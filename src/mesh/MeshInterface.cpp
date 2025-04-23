#include <cstdint>
#include "MeshInterface.h"
#include "MeshTypes.h"
#include "debug.h"
#include "Default.h"
#include "MeshRadio.h"
#include "airtime.h"
#include "Channels.h"
#include "RadioInterface.h"
#include "main.h"

#define MAX_RX_FROMRADIO                                                                                                         \
    4 // max number of packets destined to our queue, we dispatch packets quickly so it doesn't need to be big


meshtastic_LocalConfig config;

static MemoryDynamic<meshtastic_MeshPacket> staticPool;
Allocator<meshtastic_MeshPacket> &packetPool = staticPool;
MeshInterface *interface;

meshtastic_MeshPacket *MeshInterface::allocForSending()
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();

    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag; // Assume payload is decoded at start.
    p->from = *_nodeId;
    p->to = NODENUM_BROADCAST;
    p->hop_limit = Default::getConfiguredOrDefaultHopLimit(_loraConfig, _loraConfig->hop_limit);
    p->id = generatePacketId();
    /*
    p->rx_time =
            getValidTime(RTCQualityFromNet); // Just in case we process the packet locally - make sure it has a valid timestamp
    */
    return p;
}

void MeshInterface::releasePacket(meshtastic_MeshPacket *p)
{
    packetPool.release(p);
}

MeshInterface::MeshInterface(meshtastic_Config_LoRaConfig *loraConfig, NodeNum *nodeId, RadioInterface *iface): concurrency::OSThread("MeshInterface"), fromRadioQueue(MAX_RX_FROMRADIO) {
    _nodeId = nodeId;
    _loraConfig = loraConfig;
    _iface = iface;
    config.lora = *loraConfig;

    config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;

    fromRadioQueue.setReader(this);
}

ErrorCode MeshInterface::send(meshtastic_MeshPacket *p) {

    if (isToUs(p)) {
        LOG_ERROR("BUG! send() called with packet destined for local node!");
        releasePacket(p);
        return meshtastic_Routing_Error_BAD_REQUEST;
    } // should have already been handled by sendLocal

    // Abort sending if we are violating the duty cycle
    if (!_loraConfig->override_duty_cycle && myRegion->dutyCycle < 100) {
        float hourlyTxPercent = airTime->utilizationTXPercent();
        if (hourlyTxPercent > myRegion->dutyCycle) {
#ifdef DEBUG_PORT
            uint8_t silentMinutes = airTime->getSilentMinutes(hourlyTxPercent, myRegion->dutyCycle);
            LOG_WARN("Duty cycle limit exceeded. Aborting send for now, you can send again in %d mins", silentMinutes);
            meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
            cn->has_reply_id = true;
            cn->reply_id = p->id;
            cn->level = meshtastic_LogRecord_Level_WARNING;
            cn->time = getValidTime(RTCQualityFromNet);
            sprintf(cn->message, "Duty cycle limit exceeded. You can send again in %d mins", silentMinutes);
            service->sendClientNotification(cn);
#endif
            LOG_DEBUG("Duty Cycle exceeded");
            meshtastic_Routing_Error err = meshtastic_Routing_Error_DUTY_CYCLE_LIMIT;
            if (isFromUs(p)) { // only send NAK to API, not to the mesh
                abortSendAndNak(err, p);
            } else {
                releasePacket(p);
            }
            return err;
        }
    }

    // PacketId nakId = p->decoded.which_ackVariant == SubPacket_fail_id_tag ? p->decoded.ackVariant.fail_id : 0;
    // assert(!nakId); // I don't think we ever send 0hop naks over the wire (other than to the phone), test that assumption with
    // assert

    // Never set the want_ack flag on broadcast packets sent over the air.
    if (isBroadcast(p->to))
        p->want_ack = false;

    // Up until this point we might have been using 0 for the from address (if it started with the phone), but when we send over
    // the lora we need to make sure we have replaced it with our local address
    p->from = getFrom(p);

    /*
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // set the relayer to us
    */
    // If we are the original transmitter, set the hop limit with which we start
    if (isFromUs(p))
        p->hop_start = p->hop_limit;

    // If the packet hasn't yet been encrypted, do so now (it might already be encrypted if we are just forwarding it)

    if (!(p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag ||
          p->which_payload_variant == meshtastic_MeshPacket_decoded_tag)) {
        return meshtastic_Routing_Error_BAD_REQUEST;
    }

    fixPriority(p); // Before encryption, fix the priority if it's unset

    // If the packet is not yet encrypted, do so now
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it
        meshtastic_MeshPacket *p_decoded = packetPool.allocCopy(*p);

        auto encodeResult = perhapsEncode(p);
        if (encodeResult != meshtastic_Routing_Error_NONE) {
            LOG_DEBUG("Encode failed: %d", encodeResult);
            packetPool.release(p_decoded);
            p->channel = 0; // Reset the channel to 0, so we don't use the failing hash again
            abortSendAndNak(encodeResult, p);
            return encodeResult; // FIXME - this isn't a valid ErrorCode
        }
#if HAS_UDP_MULTICAST
        if (udpThread && config.network.enabled_protocols & meshtastic_Config_NetworkConfig_ProtocolFlags_UDP_BROADCAST) {
            udpThread->onSend(const_cast<meshtastic_MeshPacket *>(p));
        }
#endif
        packetPool.release(p_decoded);
    }

    assert(_iface); // This should have been detected already in sendLocal (or we just received a packet from outside)
    LOG_DEBUG("MeshInterface::send: Sending packet");
    return _iface->send(p);
}

void MeshInterface::sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit)
{
    //routingModule->sendAckNak(err, to, idFrom, chIndex, hopLimit);
}

void MeshInterface::abortSendAndNak(meshtastic_Routing_Error err, meshtastic_MeshPacket *p)
{
    LOG_ERROR("Error=%d, return NAK and drop packet", err);
    sendAckNak(err, getFrom(p), p->id, p->channel);
    packetPool.release(p);
}

static uint8_t bytes[MAX_LORA_PAYLOAD_LEN + 1] __attribute__((__aligned__));

/** Return 0 for success or a Routing_Error code for failure
 */
meshtastic_Routing_Error MeshInterface::perhapsEncode(meshtastic_MeshPacket *p)
{
    int16_t hash;

    // If the packet is not yet encrypted, do so now
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (isFromUs(p)) {
            p->decoded.has_bitfield = true;
            p->decoded.bitfield |= (_loraConfig->config_ok_to_mqtt << BITFIELD_OK_TO_MQTT_SHIFT);
            p->decoded.bitfield |= (p->decoded.want_response << BITFIELD_WANT_RESPONSE_SHIFT);
        }

        size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_Data_msg, &p->decoded);

        if (numbytes + MESHTASTIC_HEADER_LENGTH > MAX_LORA_PAYLOAD_LEN)
            return meshtastic_Routing_Error_TOO_LARGE;

        // printBytes("plaintext", bytes, numbytes);

        ChannelIndex chIndex = p->channel; // keep as a local because we are about to change it

        // TODO: implement PKI
        if (p->pki_encrypted == true) {
            // Client specifically requested PKI encryption
            return meshtastic_Routing_Error_PKI_FAILED;
        }
        hash = channels.setActiveByIndex(chIndex);
        LOG_DEBUG("Channel hash: %d, index: %d", hash, chIndex);
        // Now that we are encrypting the packet channel should be the hash (no longer the index)
        p->channel = hash;
        if (hash < 0) {
            // No suitable channel could be found for sending
            return meshtastic_Routing_Error_NO_CHANNEL;
        }
        crypto->encryptPacket(getFrom(p), p->id, numbytes, bytes);
        memcpy(p->encrypted.bytes, bytes, numbytes);


        // Copy back into the packet and set the variant type
        p->encrypted.size = numbytes;
        p->which_payload_variant = meshtastic_MeshPacket_encrypted_tag;
    }

    return meshtastic_Routing_Error_NONE;
}

PacketId generatePacketId()
{
    static uint32_t rollingPacketId; // Note: trying to keep this in noinit didn't help for working across reboots
    static bool didInit = false;

    if (!didInit) {
        didInit = true;

        // pick a random initial sequence number at boot (to prevent repeated reboots always starting at 0)
        // Note: we mask the high order bit to ensure that we never pass a 'negative' number to random
        rollingPacketId = random(UINT32_MAX & 0x7fffffff);
        LOG_DEBUG("Initial packet id %u", rollingPacketId);
    }

    rollingPacketId++;

    rollingPacketId &= ID_COUNTER_MASK;                                    // Mask out the top 22 bits
    PacketId id = rollingPacketId | random(UINT32_MAX & 0x7fffffff) << 10; // top 22 bits
    LOG_DEBUG("Partially randomized packet id %u", id);
    return id;
}

NodeNum MeshInterface::getFrom(const meshtastic_MeshPacket *p)
{
    return (p->from == 0) ? *_nodeId : p->from;
}

// Returns true if the packet originated from the local node
bool MeshInterface::isFromUs(const meshtastic_MeshPacket *p)
{
    return p->from == 0 || p->from == *_nodeId;
}

bool MeshInterface::isToUs(const meshtastic_MeshPacket *p) {
    return p->to == *_nodeId;
}


bool isBroadcast(uint32_t dest)
{
    return dest == NODENUM_BROADCAST || dest == NODENUM_BROADCAST_NO_LORA;
}

bool isFromUs(const meshtastic_MeshPacket *p)
{
    return p->from == 0 || p->from == interface->isFromUs(p);
}

int32_t MeshInterface::runOnce()
{
    meshtastic_MeshPacket *mp;
    while ((mp = fromRadioQueue.dequeuePtr(0)) != NULL) {
        // printPacket("handle fromRadioQ", mp);
        perhapsHandleReceived(mp);
    }

    // LOG_DEBUG("Sleep forever!");
    return INT32_MAX; // Wait a long time - until we get woken for the message queue
}

bool perhapsDecode(meshtastic_MeshPacket *p)
{
    //concurrency::LockGuard g(cryptLock);

    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER &&
        config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_ALL_SKIP_DECODING)
        return false;

    /*
    if (config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY &&
        (nodeDB->getMeshNode(p->from) == NULL || !nodeDB->getMeshNode(p->from)->has_user)) {
        LOG_DEBUG("Node 0x%x not in nodeDB-> Rebroadcast mode KNOWN_ONLY will ignore packet", p->from);
        return false;
    }
    */
    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag)
        return true; // If packet was already decoded just return

    size_t rawSize = p->encrypted.size;
    if (rawSize > sizeof(bytes)) {
        LOG_ERROR("Packet too large to attempt decryption! (rawSize=%d > 256)", rawSize);
        return false;
    }
    bool decrypted = false;
    ChannelIndex chIndex = 0;
#if !(MESHTASTIC_EXCLUDE_PKI)
    // Attempt PKI decryption first
    /*
    if (p->channel == 0 && isToUs(p) && p->to > 0 && !isBroadcast(p->to) && nodeDB->getMeshNode(p->from) != nullptr &&
        nodeDB->getMeshNode(p->from)->user.public_key.size > 0 && nodeDB->getMeshNode(p->to)->user.public_key.size > 0 &&
        rawSize > MESHTASTIC_PKC_OVERHEAD) {
        LOG_DEBUG("Attempt PKI decryption");

        if (crypto->decryptCurve25519(p->from, nodeDB->getMeshNode(p->from)->user.public_key, p->id, rawSize, p->encrypted.bytes,
                                      bytes)) {
            LOG_INFO("PKI Decryption worked!");
            memset(&p->decoded, 0, sizeof(p->decoded));
            rawSize -= MESHTASTIC_PKC_OVERHEAD;
            if (pb_decode_from_bytes(bytes, rawSize, &meshtastic_Data_msg, &p->decoded) &&
                p->decoded.portnum != meshtastic_PortNum_UNKNOWN_APP) {
                decrypted = true;
                LOG_INFO("Packet decrypted using PKI!");
                p->pki_encrypted = true;
                memcpy(&p->public_key.bytes, nodeDB->getMeshNode(p->from)->user.public_key.bytes, 32);
                p->public_key.size = 32;
            } else {
                LOG_ERROR("PKC Decrypted, but pb_decode failed!");
                return false;
            }
        } else {
            LOG_WARN("PKC decrypt attempted but failed!");
        }
    }
     */
#endif

    // assert(p->which_payloadVariant == MeshPacket_encrypted_tag);
    if (!decrypted) {
        // Try to find a channel that works with this hash
        for (chIndex = 0; chIndex < channels.getNumChannels(); chIndex++) {
            // Try to use this hash/channel pair
            if (channels.decryptForHash(chIndex, p->channel)) {
                // we have to copy into a scratch buffer, because these bytes are a union with the decoded protobuf. Create a
                // fresh copy for each decrypt attempt.
                memcpy(bytes, p->encrypted.bytes, rawSize);
                // Try to decrypt the packet if we can
                crypto->decrypt(p->from, p->id, rawSize, bytes);

                // printBytes("plaintext", bytes, p->encrypted.size);

                // Take those raw bytes and convert them back into a well structured protobuf we can understand
                memset(&p->decoded, 0, sizeof(p->decoded));
                if (!pb_decode_from_bytes(bytes, rawSize, &meshtastic_Data_msg, &p->decoded)) {
                    LOG_ERROR("Invalid protobufs in received mesh packet id=0x%08x (bad psk?)!", p->id);
                } else if (p->decoded.portnum == meshtastic_PortNum_UNKNOWN_APP) {
                    LOG_ERROR("Invalid portnum (bad psk?)!");
                } else {
                    decrypted = true;
                    break;
                }
            }
        }
    }
    if (decrypted) {
        // parsing was successful
        p->which_payload_variant = meshtastic_MeshPacket_decoded_tag; // change type to decoded
        p->channel = chIndex;                                         // change to store the index instead of the hash
        if (p->decoded.has_bitfield)
            p->decoded.want_response |= p->decoded.bitfield & BITFIELD_WANT_RESPONSE_MASK;

        /* Not actually ever used.
        // Decompress if needed. jm
        if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP) {
            // Decompress the payload
            char compressed_in[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
            char decompressed_out[meshtastic_Constants_DATA_PAYLOAD_LEN] = {};
            int decompressed_len;

            memcpy(compressed_in, p->decoded.payload.bytes, p->decoded.payload.size);

            decompressed_len = unishox2_decompress_simple(compressed_in, p->decoded.payload.size, decompressed_out);

            // LOG_DEBUG("**Decompressed length - %d ", decompressed_len);

            memcpy(p->decoded.payload.bytes, decompressed_out, decompressed_len);

            // Switch the port from PortNum_TEXT_MESSAGE_COMPRESSED_APP to PortNum_TEXT_MESSAGE_APP
            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        } */

        printPacket("decoded message", p);
#if ENABLE_JSON_LOGGING
        LOG_TRACE("%s", MeshPacketSerializer::JsonSerialize(p, false).c_str());
#elif ARCH_PORTDUINO
        if (settingsStrings[traceFilename] != "" || settingsMap[logoutputlevel] == level_trace) {
            LOG_TRACE("%s", MeshPacketSerializer::JsonSerialize(p, false).c_str());
        }
#endif
        return true;
    } else {
        LOG_WARN("No suitable channel found for decoding, hash was 0x%x!", p->channel);
        return false;
    }
}

/**
 * Handle any packet that is received by an interface on this node.
 * Note: some packets may merely being passed through this node and will be forwarded elsewhere.
 */
void MeshInterface::handleReceived(meshtastic_MeshPacket *p, RxSource src)
{

}

void MeshInterface::perhapsHandleReceived(meshtastic_MeshPacket *p)
{


    // Note: we avoid calling shouldFilterReceived if we are supposed to ignore certain nodes - because some overrides might
    // cache/learn of the existence of nodes (i.e. FloodRouter) that they should not
    handleReceived(p);
    packetPool.release(p);
}

void MeshInterface::enqueueReceivedMessage(meshtastic_MeshPacket *p)
{
    // Try enqueue until successful
    while (!fromRadioQueue.enqueue(p, 0)) {
        meshtastic_MeshPacket *old_p;
        old_p = fromRadioQueue.dequeuePtr(0); // Dequeue and discard the oldest packet
        if (old_p) {
            printPacket("fromRadioQ full, drop oldest!", old_p);
            packetPool.release(old_p);
        }
    }
    // Nasty hack because our threading is primitive.  interfaces shouldn't need to know about routers FIXME
    setReceivedMessage();
}

void MeshInterface::setReceivedMessage()
{
    // LOG_DEBUG("set interval to ASAP");
    setInterval(0); // Run ASAP, so we can figure out our correct sleep time
    runASAP = true;
}