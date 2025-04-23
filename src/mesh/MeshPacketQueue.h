#pragma once

#include "MeshTypes.h"
#include "MeshInterface.h"

#include <queue>

/**
 * A priority queue of packets
 */
class MeshPacketQueue
{
    size_t maxLen;
    std::vector<meshtastic_MeshPacket *> queue;
    NodeNum *nodeId;

    /** Replace a lower priority package in the queue with 'mp' (provided there are lower pri packages). Return true if replaced.
     */
    bool replaceLowerPriorityPacket(meshtastic_MeshPacket *mp);

    bool CompareMeshPacketFunc(const meshtastic_MeshPacket *p1, const meshtastic_MeshPacket *p2);
    bool _isFromUs(const meshtastic_MeshPacket *p);
    NodeNum _getFrom(const meshtastic_MeshPacket *p);

  public:
    explicit MeshPacketQueue(size_t _maxLen, NodeNum *_nodeId);

    /** enqueue a packet, return false if full */
    bool enqueue(meshtastic_MeshPacket *p);

    /** return true if the queue is empty */
    bool empty();

    /** return amount of free packets in Queue */
    size_t getFree() { return maxLen - queue.size(); }

    /** return total size of the Queue */
    size_t getMaxLen() { return maxLen; }

    meshtastic_MeshPacket *dequeue();

    meshtastic_MeshPacket *getFront();

    /** Attempt to find and remove a packet from this queue.  Returns the packet which was removed from the queue */
    meshtastic_MeshPacket *remove(NodeNum from, PacketId id, bool tx_normal = true, bool tx_late = true);

    /* Attempt to find a packet from this queue. Return true if it was found. */
    bool find(NodeNum from, PacketId id);
};