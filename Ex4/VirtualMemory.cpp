
#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <algorithm>


#define BIT_PER_TABLE ((VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH) / TABLES_DEPTH)
#define FAILURE 0
#define SUCCESS 1
struct dfsTraversalInfo {
    int currentFrame = 0;
    uint64_t parentFrame = -1;
    int maximalFrameIndex = 0;

    uint64_t cycCurFrame = 0;
    uint64_t cycParent = 0;
    uint64_t cycPage = 0;
    uint64_t maximalCycDistance = 0;
};


int translateAddress(uint64_t virtualAddress, int &lastFrameVisited, int
&lastLayerVisited);

void handlingPageFault(uint64_t virtualAddress, int &
lastFrameVisited, int &lastLayerVisited);

void findEmptyTable(dfsTraversalInfo &dfstraversalinfo, uint64_t
virtualAddress,
                    int &lastFrameVisited, int &lastLayerVisited);

void findUnusedFrame(dfsTraversalInfo &dfstraversalinfo, uint64_t
virtualAddress, int &lastFrameVisited, int &lastLayerVisited);

void evictPage(dfsTraversalInfo &dfstraversalinfo, uint64_t virtualAddress,
               int &lastFrameVisited, int &lastLayerVisited);

dfsTraversalInfo
dfsAlgorithm(uint64_t virtualAddressToSwapIn, int lastFrameVisited);

void dfsHelper(uint64_t virtualAddress, int curFrame, uint64_t parent,
               dfsTraversalInfo &dfstraversalinfo,
               int lastFrameVisited, int numOfLayer,
               uint64_t virtualAddressPath);

void clearTable(uint64_t frameNumber);

void processTableEntries(uint64_t virtualAddress, int currentFrame, uint64_t
parent, dfsTraversalInfo &dfstraversalinfo,
                         int lastFrameVisited, int numOfLayer, uint64_t
                         virtualAddressPath);


uint64_t extractBits(uint64_t address, int start, int length) {
    return (address >> start) & ((1ULL << length) - 1);
}


int VMread(uint64_t virtualAddress, word_t *value) {

    if (virtualAddress > VIRTUAL_MEMORY_SIZE) {
        return FAILURE;
    }

    int lastFrameVisited = 0;
    int lastLevel = 0;

    uint64_t offset = extractBits(virtualAddress, 0, OFFSET_WIDTH);
    uint64_t physicalAddress = translateAddress(virtualAddress,lastFrameVisited, lastLevel);
    if (physicalAddress != 0) {
        PMread(physicalAddress * PAGE_SIZE + offset, value);
        return SUCCESS;
    }
    handlingPageFault(virtualAddress, lastFrameVisited, lastLevel);
    PMread(lastFrameVisited * PAGE_SIZE + offset, value);
    return SUCCESS;

}


int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress > VIRTUAL_MEMORY_SIZE) {
        return FAILURE;
    }
    int lastFrameVisited = 0;
    int lastLayerVisited = 0;

    uint64_t offset = extractBits(virtualAddress, 0, OFFSET_WIDTH);
    uint64_t physicalAddress = translateAddress(virtualAddress,
                                                lastFrameVisited,lastLayerVisited);
    if (physicalAddress != 0) {
        PMwrite(physicalAddress * PAGE_SIZE + offset, value);
        return SUCCESS;
    }
    handlingPageFault(virtualAddress, lastFrameVisited, lastLayerVisited);
    PMwrite(lastFrameVisited * PAGE_SIZE + offset, value);
    return SUCCESS;

}


int translateAddress(uint64_t virtualAddress, int &lastFrameVisited,
                     int &lastLayerVisited) {

    int frameNumber = 0;
    for (int layer = 0; layer < TABLES_DEPTH; layer++) {

        int startPosIndex = OFFSET_WIDTH + (TABLES_DEPTH - 1 - layer)* BIT_PER_TABLE;
        uint64_t entry = extractBits(virtualAddress, startPosIndex,
                                     BIT_PER_TABLE);
        lastFrameVisited = frameNumber;
        PMread(frameNumber * PAGE_SIZE + entry, &frameNumber);
        if (frameNumber == 0) {
            lastLayerVisited = layer;
            return FAILURE; // page fault
        }
    }
    return frameNumber;
}

dfsTraversalInfo
dfsAlgorithm(uint64_t virtualAddressToSwapIn, int lastFrameVisited) {
    dfsTraversalInfo dfstraversalinfo;
    dfsHelper(virtualAddressToSwapIn, 0, -1,
              dfstraversalinfo, lastFrameVisited, 0, 0); // Start DFS from the
    // root
    // frame (0)
    return dfstraversalinfo;
}

void dfsHelper(uint64_t virtualAddress, int curFrame, uint64_t parent,
               dfsTraversalInfo &dfstraversalinfo, int lastFrameVisited,
               int numOfLayer, uint64_t virtualAddressPath) {
    if (numOfLayer >= TABLES_DEPTH) {
        uint64_t desiredPage = extractBits(virtualAddress, 0, OFFSET_WIDTH);
        // Calculate the distance between the pages
        uint64_t dist1 = (desiredPage > virtualAddressPath) ? (desiredPage -
                                                               virtualAddressPath): (
                                 virtualAddressPath - desiredPage);
        uint64_t dist2 = NUM_PAGES - dist1;
        uint64_t minDistance = (dist1 > dist2) ? dist2 : dist1;

        if (minDistance > dfstraversalinfo.maximalCycDistance) {
            dfstraversalinfo.cycParent = parent;
            dfstraversalinfo.cycPage = virtualAddressPath;
            dfstraversalinfo.cycCurFrame = curFrame;
            dfstraversalinfo.maximalCycDistance = minDistance;
        }
        dfstraversalinfo.maximalFrameIndex =
                std::max(dfstraversalinfo.maximalFrameIndex,curFrame);
        return;
    }

    processTableEntries(virtualAddress, curFrame, parent, dfstraversalinfo,
                        lastFrameVisited, numOfLayer, virtualAddressPath);
}


void findEmptyTable(dfsTraversalInfo &dfstraversalinfo, uint64_t
virtualAddress, int &lastFrameVisited, int &lastLayerVisited) {
    // Clear parent entry:
    PMwrite(dfstraversalinfo.parentFrame, 0);
    int startPosIndex = OFFSET_WIDTH + (TABLES_DEPTH - 1 - lastLayerVisited)
            * BIT_PER_TABLE;
    uint64_t entry = extractBits(virtualAddress, startPosIndex, BIT_PER_TABLE);
    PMwrite(lastFrameVisited * PAGE_SIZE + entry, dfstraversalinfo
            .currentFrame);
    lastFrameVisited = dfstraversalinfo.currentFrame;
    lastLayerVisited++;
}

void findUnusedFrame(dfsTraversalInfo &dfstraversalinfo, uint64_t
virtualAddress, int &lastFrameVisited, int &lastLayerVisited) {

    int startPosIndex = OFFSET_WIDTH + (TABLES_DEPTH - 1 - lastLayerVisited)
            * BIT_PER_TABLE;
    uint64_t entry = extractBits(virtualAddress, startPosIndex, BIT_PER_TABLE);
    int newFrame = dfstraversalinfo.maximalFrameIndex + 1;
    PMwrite(lastFrameVisited * PAGE_SIZE + entry, newFrame);
    lastFrameVisited = newFrame;
    clearTable(newFrame);
    lastLayerVisited++;
}

void evictPage(dfsTraversalInfo &dfstraversalinfo, uint64_t virtualAddress,
               int &lastFrameVisited, int &lastLayerVisited) {
    // Evict page
    PMwrite(dfstraversalinfo.cycParent, 0);
    PMevict(dfstraversalinfo.cycCurFrame, dfstraversalinfo.cycPage);
    clearTable(dfstraversalinfo.cycCurFrame);
    int startPosIndex = OFFSET_WIDTH + (TABLES_DEPTH - 1 - lastLayerVisited)
            * BIT_PER_TABLE;
    uint64_t entry = extractBits(virtualAddress, startPosIndex, BIT_PER_TABLE);
    PMwrite(lastFrameVisited * PAGE_SIZE + entry, static_cast<int>
    (dfstraversalinfo.cycCurFrame));
    lastFrameVisited = static_cast<int>(dfstraversalinfo.cycCurFrame);
    lastLayerVisited++;
}

void handlingPageFault(uint64_t virtualAddress, int &lastFrameVisited,
                       int &lastLayerVisited) {
    int depthCount = TABLES_DEPTH - lastLayerVisited;

    while (depthCount > 0) {
        dfsTraversalInfo dfstraversalinfo = dfsAlgorithm(virtualAddress,
                                                         lastFrameVisited);

        if (dfstraversalinfo.currentFrame != 0 && static_cast<int>
                                                  (dfstraversalinfo.parentFrame) != -1) {
            findEmptyTable(dfstraversalinfo, virtualAddress, lastFrameVisited,
                           lastLayerVisited);
        } else if (dfstraversalinfo.maximalFrameIndex < NUM_FRAMES - 1) {
            findUnusedFrame(dfstraversalinfo, virtualAddress, lastFrameVisited,
                            lastLayerVisited);
        } else {
            evictPage(dfstraversalinfo, virtualAddress, lastFrameVisited,
                      lastLayerVisited);
        }
        depthCount--;
    }
    PMrestore(lastFrameVisited, virtualAddress >> OFFSET_WIDTH);
}

void processTableEntries(uint64_t virtualAddress, int currentFrame, uint64_t
parent, dfsTraversalInfo &dfstraversalinfo, int lastFrameVisited, int
numOfLayer, uint64_t virtualAddressPath) {

    bool allEntriesZero = true;

    for (int entryIndex = 0; entryIndex < PAGE_SIZE; ++entryIndex) {
        int entryValue;
        PMread(currentFrame * PAGE_SIZE + entryIndex, &entryValue);

        if (entryValue != 0) {
            allEntriesZero = false;
            dfstraversalinfo.maximalFrameIndex = std::max(dfstraversalinfo
                                                                  .maximalFrameIndex,
                                                          entryValue);

            uint64_t shiftAmount = (numOfLayer < TABLES_DEPTH - 1) ?
                    BIT_PER_TABLE : OFFSET_WIDTH;
            uint64_t shiftedVirtualAddressPath =
                    virtualAddressPath << shiftAmount;
            uint64_t updatedVirtualAddressPath =
                    shiftedVirtualAddressPath | entryIndex;

            dfsHelper(virtualAddress, entryValue, currentFrame * PAGE_SIZE +
                                                  entryIndex, dfstraversalinfo,
                      lastFrameVisited, numOfLayer + 1,
                      updatedVirtualAddressPath);
        }
    }

    if (allEntriesZero && lastFrameVisited != currentFrame) {
        dfstraversalinfo.currentFrame = currentFrame;
        dfstraversalinfo.parentFrame = parent;
    }
}


void clearTable(uint64_t frameNumber) {
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        PMwrite(frameNumber * PAGE_SIZE + i, 0);
    }
}

void VMinitialize() {
    clearTable(0);
}
