#include "process.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

// Process class methods
Process::Process(ProcessDetails details, uint64_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    lastState = state;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }

    is_interrupted = false;
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    remain_time = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        remain_time += burst_times[i];
    }
}

Process::~Process()
{
    delete[] burst_times;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint64_t Process::getLaunchTime() const
{
    return launch_time;
}


uint32_t Process::getStartTime() const
{
    return start_time;
}

uint8_t Process::getPriority() const
{
    return priority;
}

uint16_t Process::get_current_burst_id() const
{
    return current_burst;
}

uint64_t Process::getBurstStartTime() const
{
    return burst_start_time;
}
//added 04/01/2021
uint64_t Process::getCurrentBurstTime() const
{
    return burst_times[current_burst];
}

Process::State Process::getState() const
{
    return state;
}

Process::State Process::getLastState() const
{
    return lastState;
}

bool Process::isInterrupted() const
{
    return is_interrupted;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

double Process::getRemainingTime() const
{
    return (double)remain_time / 1000.0;
}

void Process::setBurstStartTime(uint64_t current_time)
{
    burst_start_time = current_time;
}

void Process::setState(State new_state, uint64_t current_time)
{
    if (state == State::NotStarted && new_state == State::Ready)
    {
        launch_time = current_time;
    }
    state = new_state;
}



bool Process::isLastBurst() const{

    if(current_burst >= (num_bursts - 1)){
        
        return true;
    }

    return false;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::setLastState(State state, uint64_t current_time)
{
    lastState = state;
}

void Process::interrupt()
{
    is_interrupted = true;
}

void Process::interruptHandled()
{
    is_interrupted = false;
}

void Process::updateProcess(uint64_t current_time)
{
    // use `current_time` to update turnaround time, wait time, burst times, 
    // cpu time, and remaining time
    if(state != Terminated && ((state == Ready) || (state == Running) || (state == IO) )){
        turn_time = current_time - launch_time;
    }
    //printf("%" PRIu32 "\n", turn_time);



    //updateBurstTime(current_burst, current_time - burst_start_time); 
    
    if(state == Running){
        printf("ct is %ld\n",current_time);
        printf("ss is %ld\n", state_start);
        cpu_time = cpu_time + (current_time-state_start); 
            //printf("%" PRIu32 "\n", cpu_time);
    }
    else{
    
        wait_time = turn_time - cpu_time; 
    }   
    
    
    remain_time = remain_time - cpu_time; //wrong

    //printf("%" PRIu32 "\n", remain_time);

}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}


void Process::incrementBurstIdx()
{
    current_burst++;
}


// Comparator methods: used in std::list sort() method
// No comparator needed for FCFS or RR (ready queue never sorted)

// SJF - comparator for sorting ready queue based on shortest remaining CPU time
//This will return true if p1 is the faster job and false if p2 is the faster job.
bool SjfComparator::operator ()(const Process *p1, const Process *p2)
{
    // your code here!
    if (p1->getRemainingTime() > p2->getRemainingTime()){
        return false;

    }
    return true; // change this!
}

// PP - comparator for sorting read queue based on priority
bool PpComparator::operator ()(const Process *p1, const Process *p2){

    if(p1->getPriority()> p2->getPriority()){

        return true;
    }
    else if (p1->getPriority() <p2->getPriority()){

        return false;
    }
    else{//they are equal
        if(p1->getLaunchTime() < p2->getLaunchTime()){
            return true;
        }
        else{
            return false;
        }
    }

    return false; 
}
