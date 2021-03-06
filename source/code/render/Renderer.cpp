#include "Renderer.h"

namespace eigen
{

    Error Renderer::initialize(const Config& config)
    {
        assert(_frameNumber == 0);
        _frameNumber = 1;
        _config = config;

        _scratchMem = AllocateMemory<int8_t>(config.allocator, config.scratchSize);
        _scratchAllocPtr = _scratchMem;
        _scratchAllocEnd = _scratchMem + config.scratchSize/2;

        _deadMeat.initialize(config.allocator, 64);
        _binAgent.initialize(config.allocator, 2048);
        _planManager.initialize(config.allocator, 8);
        return platformInit(config);    // see e.g. RendererDx11.cpp
    }

    void Renderer::cleanup()
    {
        assert(_frameNumber > 0);

        _workCoordinator.stop();

        while (_deadMeat.getCount())
        {
            DeadMeat& meat = _deadMeat.at(0);
            meat.deleteFunc(meat.object);
            _deadMeat.removeFirst();
        }

        FreeMemory(_scratchMem);
        platformCleanup();
        _frameNumber = 0;
    }

    Renderer::~Renderer()
    {
        if (_frameNumber > 0)
        {
            cleanup();
        }
    }

    RenderPlanPtr Renderer::createPlan()
    {
        return _planManager.create();
    }

    void DestroyRefCounted(RenderPlan* plan)
    {
        Renderer& renderer = *StructFromMember(&Renderer::_planManager, plan->getManager());
        renderer.scheduleDeletion(plan, 1);
    }

    BatchQueue* Renderer::openBatchQueue(RenderPlan* plan)
    {
        if (Failed(plan->validate()))
        {
            return nullptr;
        }

        BatchQueue* batchQ = BatchQueue::Create(this, plan);
        batchQ->_next = _openBatchQueueHead;
        _openBatchQueueHead = batchQ;
        return batchQ;
    }

    void Renderer::commenceWork()
    {
        // Ensure that no batchQs were left open
        // Also reverse the open list to put it back into API order to aid debugging

        BatchQueue* head = _openBatchQueueHead;
        BatchQueue** tail = &head;
        for (; _openBatchQueueHead; _openBatchQueueHead = _openBatchQueueHead->_next)
        {
            if (_openBatchQueueHead->_renderer)
            {
                // error TODO
                assert(false);      // must call BatchQueue::finish() on all open batchQs before commencing work
                return;
            }
            *tail = _openBatchQueueHead;
            tail = &(*tail)->_next;
        }
        *tail = nullptr;

        //if (_frameNumber % 100 == 0)
        //{
        //    printf("Submitting frame #%d\n", _frameNumber);
        //}
        _workCoordinator.sync();

        _displayManager.presentAll(_frameNumber);

        _workCoordinator.prepareWork(head);
        _workCoordinator.kick();

        if (_scratchAllocPtr > _scratchAllocEnd)
        {
            // TODO grow scratch buffer or spew error?
            // note: must defer deallocation of current one until next frame if reallocting
        }

        // Toggle double-buffered frame alloc segment of scratch mem

        if (_scratchAllocEnd == _scratchMem + _config.scratchSize/2)
        {
            _scratchAllocPtr  = _scratchAllocEnd;
            _scratchAllocEnd += _config.scratchSize/2;
        }
        else
        {
            _scratchAllocPtr  = _scratchMem;
            _scratchAllocEnd -= _config.scratchSize/2;
        }

        // Perform delayed destruction on resources

        while (_deadMeat.getCount() && _deadMeat.at(0).frameNumber == _frameNumber)
        {
            DeadMeat& meat = _deadMeat.at(0);
            meat.deleteFunc(meat.object);
            _deadMeat.removeFirst();
        }

        _frameNumber++;
    }
}
