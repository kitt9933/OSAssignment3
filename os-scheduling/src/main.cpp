#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <algorithm>

//
// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;//may need to use this for process if it is interrupted or not - Kong - 03/31/2021
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
    
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint64_t currentTime();
std::string processStateToString(Process::State state);


int main(int argc, char **argv)
{
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }


    //printf("start main \n");

    // Declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    //printf("read configure file \n");

    // Store configuration parameters in shared data object
    // put mutex locks here?? 03/31/2021
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();


    
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            
            
            {
                std::lock_guard<std::mutex>lock(shared_data->mutex);
                shared_data->ready_queue.push_back(p);
                //std::lock_guard<std::mutex>unlock(shared_data->mutex);
            }
        }
    }

    // Free configuration data from memory
    deleteConfig(config);
    

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data ); //i, shared_data
    }

    

    // Main thread work goes here
    int num_lines = 0;
    while (!(shared_data->all_terminated))
    {

        
        // Clear output from previous iteration
        clearOutput(num_lines);

        // Do the following:
        //   - Get current time
        uint64_t cTime = currentTime();

        
        for(int i = 0; i < processes.size(); i++){//go through each process
            
            //printf("going through processes and conditions \n");

            processes[i]->updateProcess(cTime); 
            //   - *Check if any processes need to move from NotStarted to Ready (based on elapsed time), and if so put that process in the ready queue
            if((processes[i]->getStartTime() < (cTime - start))&& processes[i]->getLastState() == processes[i]->NotStarted){ 
                

                
                {
                    std::lock_guard<std::mutex>lock(shared_data->mutex);
                    processes[i]->setState(processes[i]->Ready,cTime);
                    shared_data->ready_queue.push_back(processes[i]);
                    //std::lock_guard<std::mutex>unlock(shared_data->mutex);
                }
            }



            //   - *Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
            //If it finishes IO does it go bacj to ready state?
            else if((processes[i]->getState() == processes[i]->IO) && processes[i]->getCurrentBurstTime() < (cTime - processes[i]->getBurstStartTime()) ){ //maybe check this
                
                //printf("else if statement inside processes \n");

                {
                    std::lock_guard<std::mutex>lock(shared_data->mutex);
                    shared_data->ready_queue.push_back(processes[i]);
                    //processes[i]->setBurstStartTime(cTime);
                    
                    processes[i]->incrementBurstIdx();
                    
                    processes[i]->setState(processes[i]->Ready,cTime);
                    //std::lock_guard<std::mutex>unlock(shared_data->mutex);
                }


            }
            //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
            
            if(processes[i]->getState() == processes[i]->Running){
                {
                    //printf("running state \n");

                    std::lock_guard<std::mutex>lock(shared_data->mutex);
                    
                    if(shared_data->algorithm == RR && (shared_data->time_slice <= cTime - processes[i]->getBurstStartTime())){
                        processes[i]->interrupt();

                    }

                    bool shouldInterrupt = false;

                    for(int j = 0; j < processes.size(); j++){
                        
                        if (processes[i]->getPriority() < processes[j]->getPriority()){
                            
                            shouldInterrupt = true;
                        }
                    }
                    if(shared_data->algorithm == PP && shouldInterrupt){
                        
                        processes[i]->interrupt();
                    }
                    //std::lock_guard<std::mutex>unlock(shared_data->mutex);


                    //need to handle interupt here?
                }
            }

        }
        
        
        //   - *Sort the ready queue (if needed - based on scheduling algorithm)
    
        if(shared_data->algorithm == SJF){
            shared_data->ready_queue.sort(SjfComparator());
        }
        else if(shared_data->algorithm == PP){
            shared_data->ready_queue.sort(PpComparator());
        }

        

        //   - Determine if all processes are in the terminated state
        {
            std::lock_guard<std::mutex>unlock(shared_data->mutex);
            if(shared_data->all_terminated){
                
                return 0;
            }
        }       
        //   - * = accesses shared data (ready queue), so be sure to use proper synchronization

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);
        

        




        // sleep 50 ms
        usleep(50000);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }




    // print final statistics

    
    double cpuTotal = 0;
    double totalTurn = 0;
    double turnArray[processes.size()];
    double totalWait = 0;
    for(int j = 0; j < processes.size(); j++){

            cpuTotal = cpuTotal + processes[j]->getCpuTime();
            totalTurn = totalTurn + processes[j]->getTurnaroundTime();
            turnArray[j] = processes[j]->getTurnaroundTime();
            totalWait = totalWait + processes[j]->getWaitTime();
    }

    int n = sizeof(turnArray)/sizeof(turnArray[0]);
    std::sort(turnArray,turnArray+n);

    //  - CPU utilization
    double cpuUtil = cpuTotal/totalTurn;
    printf("Total CPU utilization is %f\n",cpuUtil);
    //  - Throughput
    //     - Average for first 50% of processes finished
    double firstTotal = 0;
    for(int r = 0; r < n/2; r++){
        firstTotal = firstTotal + turnArray[r];
    }   
    double firstAvg = firstTotal/(n/2);
    printf("First half Turnaround Average is %f\n", firstAvg);


    //     - Average for second 50% of processes finished
    double secondTotal = 0;
    for(int t = (n/2); t < n; t++){
        secondTotal = secondTotal + turnArray[t];
    }   
    double secondAvg = secondTotal/(n/2);
    printf("Second half Turnaround Average is %f\n", secondAvg);


    //     - Overall average
    double overallAvg = (firstAvg + secondAvg)/2;

    //  - Average turnaround time
    double turnAvg = totalTurn/processes.size();
    printf("Average Turnaround Time is %f\n",turnAvg);
    //  - Average waiting time
    double waitAvg = totalWait/processes.size();
    printf("Average wait time is %f\n", waitAvg);
    // Clean up before quitting program
    processes.clear();

    return 0;
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    // Work to be done by each core idependent of the other cores
    // Repeat until all processes in terminated state:

    uint64_t curTime = currentTime();

    while(!shared_data->all_terminated){

        

        //   - *Get process at front of ready queue
        
        Process *currPro;
        
            
            if(!shared_data->ready_queue.empty())
            {
                std::lock_guard<std::mutex>lock(shared_data->mutex);
                currPro = shared_data->ready_queue.front();
                shared_data->ready_queue.pop_front();
                currPro->setCpuCore(core_id);
                currPro->setBurstStartTime(curTime);
                
            
                //std::lock_guard<std::mutex>unlock(shared_data->mutex);

            }
           
        

    
        //   - Simulate the processes running until one of the following:
        currPro->setState(currPro->Running, curTime);
        currPro->incrementBurstIdx();
        currPro->setBurstStartTime(curTime);
        
        bool isEmpty;
        bool isTerminated;
        {
            std::lock_guard<std::mutex>lock(shared_data->mutex);
            isEmpty = shared_data->ready_queue.empty();
            isTerminated = shared_data->all_terminated;
        }
        
        while(!isEmpty){ //PROBLEM
        //continue running until all processes are either terminated or no more processes on either queue??
        //-- Kong's comment
        // this is okay and resolved for now -- 04/02/2021

            //printf("inwhile\n");

            uint64_t elapsed = curTime -currPro->getBurstStartTime();

            //     - CPU burst time has elapsed
            if(elapsed > currPro->getCurrentBurstTime() && currPro->getState() == currPro->Running){ 
                //switch to IO or terminated
                
                

                if(currPro->getRemainingTime() > 0){
                    currPro->setState(currPro->IO, curTime);
                    //currPro->incrementBurstIdx();
                    //currPro->setBurstStartTime(curTime);
                    
                }
                else{
                    currPro->setState(currPro->Terminated, curTime);
                    currPro->setCpuCore(-1);
                }
            }
            
            //     - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
            //double check this!!

            {
                std::lock_guard<std::mutex>lock(shared_data->mutex);
                if((shared_data->time_slice > elapsed && shared_data->algorithm == RR) || ((currPro->getPriority() > shared_data->ready_queue.front()->getPriority()) &&  shared_data->algorithm == PP)){
                    //go back to ready
                    
                    
                
                
                    currPro->setState(currPro->Ready, curTime);
                    //maybe update bursttime?
                    shared_data->ready_queue.push_back(currPro);

                }
            }
            
        }
    
        

    //  - Place the process back in the appropriate queue
    
    //     - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
    //State state; 
    // double check this - 04/01/2021
    
    if(((curTime - currPro->getBurstStartTime()) > currPro->getCurrentBurstTime()) && currPro->getState() == currPro->Running){

            currPro->setState(currPro->IO, curTime);
            
            currPro->incrementBurstIdx();
            currPro->setBurstStartTime(curTime);

            
    }
    
    //     - Terminated if CPU burst finished and no more bursts remain -- no actual queue, simply set state to Terminated
    // double check this - 04/01/2021
    if(((curTime - currPro->getBurstStartTime()) > currPro->getCurrentBurstTime()) && currPro->isLastBurst() && currPro->getState() == currPro->Running){
            
            

            currPro->setState(currPro->Terminated, curTime);
    }

    //     - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)
    //finished up this if statement - 04/02/21
    if(currPro->isInterrupted()){

        {
            std::lock_guard<std::mutex>lock(shared_data->mutex);

            currPro->setState(currPro->Ready, curTime);
            shared_data->ready_queue.push_back(currPro);

            uint64_t modifiedCPUBurstTime = currPro->getRemainingTime();

            currPro->updateBurstTime(currPro->get_current_burst_id(), modifiedCPUBurstTime);

            //std::lock_guard<std::mutex>unlock(shared_data->mutex);

        }
        
        
    }
    
    //  - Wait context switching time
    // sleeps for 500 miliseconds
    
    usleep(500000);

    //  - * = accesses shared data (ready queue), so be sure to use proper synchronization
    }
}

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;

    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }

    rewind(stdout);
    fflush(stdout);
}

uint64_t currentTime()
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
