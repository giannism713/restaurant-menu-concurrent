#include <atomic>  /* ? */
#include <cassert> /* ? */
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>  /* ? */
#include <pthread.h> /* ? */
#include <string>

#ifndef N_THREADS
#define N_THREADS 4 // Default value, if Makefile does not provide any
#endif
#define DIST (N_THREADS / 2)
#define AGNT (N_THREADS / 4)
#define COOK (N_THREADS / 4)
#define CAPACITY DIST /* ? */
#define DUMMY_VALUE 999999
#define DUMMY_VALUE_NEG -1
#define CALL_EXCHANGER 999998

#define TIMEOUT -10

/* CAS Macro for Visual Purposes */
#define CAS(ptr, old, new) (__sync_bool_compare_and_swap((ptr), (old), (new)))

static_assert(N_THREADS > 0);
static_assert((N_THREADS % 4) == 0);

using Order = size_t;

/* Pending Orders Stack implementation (Unbounded Lock-Free Elimination
 * BackOff) */
struct PendingOrdersStack
{
    // TODO
    Order order;
    PendingOrdersStack* next;
};
/* This is behaved like its an atomic value because i CAS it whenever I want to change it */
PendingOrdersStack* PendingOrdersTop = NULL;

/* What the Exchanger Array will contain */
struct ExchangerElement
{
    Order order;
    enum State
    {
        EMPTY,
        WAITING,
        BUSY
    } state;
};

std::atomic<ExchangerElement> ExchangerArray[CAPACITY];

/* Returns the current time in the program */
long getNanos()
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

/* Exchanger function Implementation */
int Exchange(std::atomic<ExchangerElement>& ExchangerSlot, Order value, long timeout)
{
    /* Time we got for a successful handshake */
    long timeBound = getNanos() + timeout;
    while (true)
    {
        /* If a lot of time has passed and its time for timeout */
        if (getNanos() > timeBound)
        {
            return TIMEOUT;
        }
        ExchangerElement givenSlot = ExchangerSlot.load(); /* Load given slot */
        switch (givenSlot.state)
        {
        case ExchangerElement::EMPTY:
        {
            ExchangerElement newElement2Put;
            newElement2Put.order = value;
            newElement2Put.state = ExchangerElement::WAITING;
            if (ExchangerSlot.compare_exchange_strong(givenSlot, newElement2Put))
            {
                /* As long as we have time */
                while (getNanos() < timeBound)
                {
                    ExchangerElement getSlot = ExchangerSlot.load(); /* Reload it to check it's state now */
                    /* That means we got a successful exchange because now it became busy */
                    if (getSlot.state == ExchangerElement::BUSY)
                    {
                        /* Remove everything from this exchange slot */
                        ExchangerElement ResetElement;
                        ResetElement.order = DUMMY_VALUE;
                        ResetElement.state = ExchangerElement::EMPTY;
                        ExchangerSlot.store(ResetElement);
                        /* Since the handshake was successful return the value and not push or pop anything */
                        return getSlot.order;
                    }
                }
                ExchangerElement ResetElement;
                ResetElement.order = DUMMY_VALUE;
                ResetElement.state = ExchangerElement::EMPTY;
                /* If it is the same as before it means noone came and we timeout since the handshake failed */
                if (ExchangerSlot.compare_exchange_strong(newElement2Put, ResetElement)) return TIMEOUT;
                else /* Someone came last second thus the CAS failed, complete the exchange */
                {
                    ExchangerElement getSlot = ExchangerSlot.load();
                    ExchangerSlot.store(ResetElement);
                    return getSlot.order;
                }
            }
            break;
        }
        case ExchangerElement::WAITING:
        {
            ExchangerElement WaitingElement;
            WaitingElement.order = givenSlot.order;
            WaitingElement.state = ExchangerElement::WAITING;
            /* Check this */
            ExchangerElement newElement2Put;
            newElement2Put.order = value;
            newElement2Put.state = ExchangerElement::BUSY;
            /* If the same element is here and some thread looks for it, just get it */
            if (ExchangerSlot.compare_exchange_strong(WaitingElement, newElement2Put))
            {
                return givenSlot.order;
            }
            break;
        }
        case ExchangerElement::BUSY:
            break;
        }
    }
}

/* Function to access the elimination array of the Pending Orders Stack */
int Visit(Order value, int range, int duration)
{
    int el = rand() % range;
    return Exchange(ExchangerArray[el], value, duration); /* Passed by reference */
}

/* Try Push function for Pending Orders Stack */
bool TryPush(PendingOrdersStack* NewNode)
{
    PendingOrdersStack* oldTop = PendingOrdersTop;
    NewNode->next = oldTop;
    return CAS(&PendingOrdersTop, oldTop, NewNode);
}

/* Push function for Pending Orders Stack */
void PushOrder(Order x)
{
    int range;
    long duration;

    PendingOrdersStack* NewNode = (PendingOrdersStack*)malloc(sizeof(struct PendingOrdersStack));
    NewNode->order = x;
    while (true)
    {
        if (TryPush(NewNode)) return;
        else
        {
            range = CAPACITY;
            duration = 1000000; // 1ms (ns by default so we good)
            int otherValue = Visit(x, range, duration);
            if (otherValue != TIMEOUT)
            {
                return;
            }
        }
    }
}

/* Try Pop function for Pending Orders Stack */
int TryPop(void)
{
    PendingOrdersStack* oldTop = PendingOrdersTop;
    PendingOrdersStack* newTop;
    if (oldTop == NULL)
    {
        return DUMMY_VALUE; /* That means the Stack is empty */
    }
    newTop = oldTop->next;
    if (CAS(&PendingOrdersTop, oldTop, newTop))
    {
        /* Its a safe cast all orders are greater than zero */
        return (int)oldTop->order;
    }
    else
        return CALL_EXCHANGER; /* CAS failed so someone else touched top check if we can save sometime by exchanging */
}

/* Pop function for Pending Orders Stack */
int PopOrder()
{
    int range;
    long duration;

    int returnValue;
    while (true)
    {
        returnValue = TryPop();
        if (returnValue == CALL_EXCHANGER)
        {
            /* Call exchanger */
            range = CAPACITY;
            duration = 1000000; /* 1ms (ns by default so we good) */
            int otherValue = Visit(0, range, duration);
            if (otherValue != TIMEOUT)
            {
                return otherValue;
            }
        }
        else if (returnValue == DUMMY_VALUE)
        {
            return DUMMY_VALUE; /* That means empty stack too */
        }
        else
            return returnValue; /* Everything went OK return the value */
    }
}

/*------------------------------------------------------*/
/* Under-Preparation Orders Queue implementation (Unbounded Total) */
struct UnderPreparationOrdersQueue
{
    // TODO
    Order order;
    UnderPreparationOrdersQueue* next;
};
UnderPreparationOrdersQueue *UnderPreparationOrdersQueueHead, *UnderPreparationOrdersQueueTail;
pthread_mutex_t QueueHeadLock, QueueTailLock;

/* Enqueue Function for the Queue of Under Preperation Orders */
void EnqueueOrder(Order x)
{
    UnderPreparationOrdersQueue* NewNode = (UnderPreparationOrdersQueue*)malloc(sizeof(struct UnderPreparationOrdersQueue));
    NewNode->order = x;
    NewNode->next = NULL;
    pthread_mutex_lock(&QueueTailLock);
    UnderPreparationOrdersQueueTail->next = NewNode;
    UnderPreparationOrdersQueueTail = NewNode;
    pthread_mutex_unlock(&QueueTailLock);
}

/* Dequeue Function for the Queue of Under Pre*/
Order DequeueOrder()
{
    Order result;
    pthread_mutex_lock(&QueueHeadLock);
    if (UnderPreparationOrdersQueueHead->next == NULL)
    {
        result = DUMMY_VALUE; /* That means empty queue */
    }
    else
    {
        /* Assign the actual result which next of the sentinel head */
        result = UnderPreparationOrdersQueueHead->next->order;
        /* Advance head */
        UnderPreparationOrdersQueueHead = UnderPreparationOrdersQueueHead->next;
    }
    pthread_mutex_unlock(&QueueHeadLock);
    return result;
}
/*------------------------------------------------------*/
/* Completed Orders List implementation (Lazy Synchronization) */
struct CompletedOrdersList
{
    // TODO
    int order;
    CompletedOrdersList* next;
    pthread_mutex_t lock;
    bool marked;
};

/* Validate function for lazy synchronization */
bool Validate(CompletedOrdersList* pred, CompletedOrdersList* curr)
{
    /* Make sure nodes are not logically deleted */
    if (!pred->marked && !curr->marked && pred->next == curr)
    {
        return true;
    }
    return false;
}

/* Search for a specific completed order in the Completed Orders list in the specified district */
bool SearchOrdersList(CompletedOrdersList* list, int order)
{
    CompletedOrdersList* curr;
    curr = list;
    /* Traverse the list for finding specific order */
    while (curr)
    {
        /* Check whether current we found that is valid is marked or not */
        if (!curr->marked && curr->order == order)
        {
            return true;
        }
        curr = curr->next;
    }
    return false;
}

/* Insert in the Completed Orders list at the head */
bool InsertOrdersList(CompletedOrdersList** list, int order)
{
    CompletedOrdersList *pred, *curr;
    bool result;
    bool return_flag;

    while (true)
    {
        pred = *list;
        curr = pred->next;
        /* Find the pair we want to add in between */
        while (curr->order < order)
        {
            pred = curr;
            curr = curr->next;
        }
        pthread_mutex_lock(&pred->lock);
        pthread_mutex_lock(&curr->lock);
        if (Validate(pred, curr))
        {
            /* If alreeady exists don't added this won't be used since each thread produces */
            if (order == curr->order)
            {
                result = false;
                return_flag = true;
            }
            else
            {
                /* Actual insertion */
                CompletedOrdersList* NewNode = (CompletedOrdersList*)malloc(sizeof(struct CompletedOrdersList));
                NewNode->next = curr;
                NewNode->order = order;
                pred->next = NewNode;
                result = true;
                return_flag = true;
            }
        }
        pthread_mutex_unlock(&pred->lock);
        pthread_mutex_unlock(&curr->lock);
        if (return_flag) return result;
    }
}

/* Customer Districts */
struct District
{
    CompletedOrdersList* completedOrdersHead;
    CompletedOrdersList* CompletedOrdersTail;
    size_t checksum;
};
District Districts[DIST];

/* Thread Jobs functions */
void* DistrictJob(void* arg)
{
    size_t tid = (size_t)arg;
    for (size_t i = 0; i < DIST; i++)
    {
        /* Generate the orders_id as the algorithm said and push them to the Pending Orders Stack */
        Order order_id = (tid * DIST) + i;
        PushOrder(order_id);
    }
    for (size_t i = 0; i < DIST; i++)
    {
        Order order_id = (tid * DIST) + i;
        while (!SearchOrdersList(Districts[tid].completedOrdersHead, order_id))
        {
        }
        /* If we found it increment checksum by order_id of the specific tid District */
        Districts[tid].checksum = Districts[tid].checksum + order_id;
    }
    return nullptr;
}

void* AgentJob(void*)
{
    int PoppedOrders = 0;
    while (PoppedOrders < 2 * DIST)
    {
        Order popped_order = PopOrder();
        /* Enqueue the order to the Under Preparation Order Queue make sure we don't get the dummy node */
        if (popped_order != DUMMY_VALUE)
        {
            EnqueueOrder(popped_order);
            PoppedOrders++;
        }
    }
    return nullptr;
}

void* CookJob(void*)
{
    int DequeuedOrders = 0;
    while (DequeuedOrders < 2 * DIST)
    {
        Order dequeued_order = DequeueOrder();
        /* Make sure dequeues don't contain the DUMMY_VALUE so we coun't actual dequeues */
        if (dequeued_order != DUMMY_VALUE)
        {
            size_t tid = dequeued_order / DIST; /* Maybe add floor function */
            InsertOrdersList(&Districts[tid].completedOrdersHead, dequeued_order);
            DequeuedOrders++;
        }
    }
    return nullptr;
}

std::string PassStr(bool pass) { return (pass ? "PASS" : "FAIL"); }
void PrintPendingOrdersEmpty(bool pass, size_t n)
{
    const std::string passStr = PassStr(pass);
    printf("%s PendingOrders Empty %lu\n", passStr.data(), n);
}
void PrintUnderPreparationOrdersEmpty(bool pass, size_t n)
{
    const std::string passStr = PassStr(pass);
    printf("%s UnderPreparationOrders Empty %lu\n", passStr.data(), n);
}
void PrintCompletedOrdersSize(bool pass, size_t tid, size_t n)
{
    const std::string passStr = PassStr(pass);
    printf("%s District[%lu].completedOrders Size %lu\n", passStr.data(), tid, n);
}
void PrintCompletedOrdersSum(bool pass, size_t tid, size_t sum)
{
    const std::string passStr = PassStr(pass);
    printf("%s District[%lu].completedOrders Sum %lu\n", passStr.data(), tid, sum);
}
void PrintCompletedOrdersValid(bool pass, size_t tid, size_t checksum)
{
    const std::string passStr = PassStr(pass);
    printf("%s District[%lu].completedOrders Valid %lu\n", passStr.data(), tid, checksum);
}

int main()
{
    // TODO
    srand(time(NULL));
    printf("%d\n", N_THREADS);
    /* Initialize the Pending Orders Stack the Exchanger, etc... */
    for (size_t i = 0; i < CAPACITY; i++)
    {
        ExchangerElement element;
        element.state = ExchangerElement::EMPTY;
        element.order = DUMMY_VALUE;
        ExchangerArray[i].store(element);
    }
    /* Initialize the Queue of Under Preperation orders */
    UnderPreparationOrdersQueue* newSentinel = (UnderPreparationOrdersQueue*)malloc(sizeof(struct UnderPreparationOrdersQueue));
    newSentinel->order = DUMMY_VALUE;
    newSentinel->next = NULL;
    pthread_mutex_init(&QueueHeadLock, NULL);
    pthread_mutex_init(&QueueTailLock, NULL);
    /* Make Head and Tail of the Queue point to the Sentinel */
    UnderPreparationOrdersQueueHead = newSentinel;
    UnderPreparationOrdersQueueTail = newSentinel;

    /* Initialize the District's array and initialize it */
    for (size_t i = 0; i < DIST; i++)
    {
        Districts[i].checksum = 0;
        /* Make the sentinel node, head for list of each District */
        Districts[i].completedOrdersHead = (CompletedOrdersList*)malloc(sizeof(struct CompletedOrdersList));
        Districts[i].completedOrdersHead->order = DUMMY_VALUE_NEG;
        Districts[i].completedOrdersHead->next = NULL;
        Districts[i].completedOrdersHead->marked = false;
        /* Make another sentinel node, tail for list of each District */
        Districts[i].CompletedOrdersTail = (CompletedOrdersList*)malloc(sizeof(struct CompletedOrdersList));
        Districts[i].CompletedOrdersTail->order = DUMMY_VALUE;
        Districts[i].CompletedOrdersTail->next = NULL;
        Districts[i].CompletedOrdersTail->marked = false;
        /* Connect the two sentinels */
        Districts[i].completedOrdersHead->next = Districts[i].CompletedOrdersTail;
        /* Initialize the mutexes */
        pthread_mutex_init(&Districts[i].completedOrdersHead->lock, NULL);
        pthread_mutex_init(&Districts[i].CompletedOrdersTail->lock, NULL);
    }

    /* Create District threads and make them start their job */
    pthread_t DistrictThreads[DIST];
    for (size_t i = 0; i < DIST; i++)
    {
        pthread_create(&DistrictThreads[i], NULL, DistrictJob, (void*)i);
    }

    /* Create Agent threads and make them start their job */
    pthread_t AgentThreads[AGNT];
    for (size_t i = 0; i < AGNT; i++)
    {
        pthread_create(&AgentThreads[i], NULL, AgentJob, NULL);
    }

    /* Create Cook threads and make them start their job */
    pthread_t CookThreads[COOK];
    for (size_t i = 0; i < COOK; i++)
    {
        pthread_create(&CookThreads[i], NULL, CookJob, NULL);
    }

    /*------------------------------------------------------*/

    /* Join, wait for District Threads now to end their job */
    for (size_t i = 0; i < DIST; i++)
    {
        pthread_join(DistrictThreads[i], NULL);
    }

    /* Join, wait for Agent Threads now to end their job */
    for (size_t i = 0; i < AGNT; i++)
    {
        pthread_join(AgentThreads[i], NULL);
    }

    /* Join, wait for Cook THreads now to end their job */
    for (size_t i = 0; i < COOK; i++)
    {
        pthread_join(CookThreads[i], NULL);
    }

    /*------------------------------------------------------*/
    /* Sanity Checks */
    /* Check that the Pending Orders Stack is empty */
    int StackCount = 0;
    PendingOrdersStack* tempTop = PendingOrdersTop;
    while (tempTop != NULL)
    {
        if (tempTop->order != DUMMY_VALUE)
        {
            StackCount++;
        }
        tempTop = tempTop->next;
    }
    /* Calculate the length of the list that makes the Stack the hard way */
    /* Check that the Under Preperation Queue is empty */
    int QueueCount = 0; /* The sentinel can contain the non-DUMMY-NODE value because when we dequeue we look for the next node */
    PrintUnderPreparationOrdersEmpty(DequeueOrder() == DUMMY_VALUE, QueueCount);
    UnderPreparationOrdersQueue* tempHead = UnderPreparationOrdersQueueHead;
    while (tempHead != NULL)
    {
        QueueCount++;
        tempHead = tempHead->next;
    }
    QueueCount--; /* -1 for the sentinel */
    PrintUnderPreparationOrdersEmpty(DequeueOrder() == DUMMY_VALUE, QueueCount);
    /* Check that districts are OK */
    for (size_t tid = 0; tid < DIST; tid++)
    {
        /* Check that PendingOrdersList is empty for every District */
        CompletedOrdersList* tempHeadList = Districts[tid].completedOrdersHead;
        int ListCount = 0;
        int ItemsInListSumValues = 0;
        while (tempHeadList != NULL)
        {
            /* In these if statements we also avoid counting up the sentinels nodes */
            if (tempHeadList->order != DUMMY_VALUE && tempHeadList->order != DUMMY_VALUE_NEG)
            {
                ItemsInListSumValues = ItemsInListSumValues + tempHeadList->order;
                ListCount++;
            }
            tempHeadList = tempHeadList->next;
        }
        PrintCompletedOrdersSize(ListCount == DIST, tid, ListCount);
        /* Check that the actual nodes of the List consist of the nodes that their sum meets the formula criteria */
        Order FormulaResult = (tid * DIST * DIST) + ((DIST - 1) * DIST) / 2;
        PrintCompletedOrdersSum(ItemsInListSumValues == (int)FormulaResult, tid, ItemsInListSumValues);
        /* Check that the Orders CheckSum corresponds to the sum given by the formula thus the CompletedOrdersList specified */
        PrintCompletedOrdersValid(ItemsInListSumValues == (int)Districts[tid].checksum, tid, ItemsInListSumValues);
    }

    return 0;
}
