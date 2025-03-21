#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <algorithm>

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include <sys/stat.h>

#include <time.h>

#include <dpu>

#include "headers/jpeg.h"
#include "headers/bmp.h"

#define DPU_BINARY "./bin/decoder_dpu"

using namespace dpu;

struct Batch {
    std::vector<std::string> filenames;
    std::vector<std::vector<short>> mcus;
    std::vector<std::vector<uint32_t>> metadata;
    std::vector<int> nr_allocated_dpus;
};

auto pim = DpuSet::allocate(DPU_ALLOCATE_ALL);
uint nr_dpus = pim.dpus().size();

std::queue<Batch> batched_queue;

std::mutex mtx;
std::condition_variable cv;
bool finished = false;

double total_execution_time = 0;
double mcu_offload_time = 0;
double mcu_prepare_time = 0;
double total_queue_waiting_time = 0;
double total_batch_time = 0;
double total_cpu_to_dpus_transfer_time = 0;
double total_dpu_execution_time = 0;
double total_dpus_to_cpu_transfer_time = 0;
double total_bmp_write_time = 0;
int total_offloading_times = 0;

double get_time(struct timespec *start, struct timespec *end){
    return (end->tv_sec - start->tv_sec) + ((end->tv_nsec - start->tv_nsec) / 1e9);
}

void mcu_prepare(const std::vector<std::string>& input_files){
    struct timespec execution_start, execution_end;

    clock_gettime(CLOCK_MONOTONIC, &execution_start);
    std::vector<std::vector<short>> MCU_buffer(nr_dpus, std::vector<short>(MAX_MCU_PER_DPU * 3 * 64));
    std::vector<std::vector<uint32_t>> metadata_buffer;
    std::vector<uint32_t> metadata(20 + 4 * 64);
    std::vector<std::string> filenames;
    std::vector<int> nr_allocated_dpus;

    int dpu_offset = 0, need_dpus = 0, free_dpus = nr_dpus;

    for(const auto& filename : input_files){
        Header *header = read_JPEG(filename);
        if(header == nullptr || header->valid == false){
            std::cout << filename << ": Error - Invalid JPEG\n";
            continue;
        }

        int padded_mcu_width = (header->mcu_width_real + 1) / 2 * 2;
        int padded_mcu_height = (header->mcu_height_real + 1) / 2 * 2;
        int total_padded_mcu_count = padded_mcu_width * padded_mcu_height;
        need_dpus = (total_padded_mcu_count + MAX_MCU_PER_DPU - 1) / MAX_MCU_PER_DPU;

        if(need_dpus > free_dpus) {
            metadata_buffer.resize(nr_dpus, std::vector<uint32_t>(20 + 4 * 64));
            {
                std::lock_guard<std::mutex> lock(mtx);
                batched_queue.push({filenames, MCU_buffer, metadata_buffer, nr_allocated_dpus});
            }
            cv.notify_one();
            
            MCU_buffer = std::vector<std::vector<short>>(nr_dpus, std::vector<short>(MAX_MCU_PER_DPU * 3 * 64));
            metadata_buffer.clear();
            filenames.clear();
            nr_allocated_dpus.clear();
            dpu_offset = 0;
            free_dpus = nr_dpus;
        }

        if(need_dpus > nr_dpus){
            std::cout << filename << ": Error - Too high resolution\n";
            continue;
        }

        free_dpus -= need_dpus;

        filenames.push_back(filename);
        nr_allocated_dpus.push_back(need_dpus);

        metadata[0] = header->mcu_height;
        metadata[1] = header->mcu_width;
        metadata[2] = header->mcu_height_real;
        metadata[3] = header->mcu_width_real;
        metadata[4] = header->num_components;
        metadata[5] = header->v_sampling_factor;
        metadata[6] = header->h_sampling_factor;

        for(uint j=0; j<header->num_components; j++)
            metadata[j + 7] = header->color_components[j].QT_ID;
        for(uint j=0; j<header->num_components; j++)
            metadata[j + header->num_components + 7] = header->color_components[j].h_sampling_factor;
        for(uint j=0; j<header->num_components; j++)
            metadata[j + (header->num_components * 2) + 7] = header->color_components[j].v_sampling_factor;
        metadata[17] = header->height;
        metadata[18] = header->width;
        metadata[19] = MAX_MCU_PER_DPU;
        for(uint j=0; j<4; j++){
            if(!header->quantization_tables[j].set) break;
                for(uint k=0; k<64; k++){
                    metadata[20 + j * 64 + k] = header->quantization_tables[j].table[k];
                }
        }
        metadata_buffer.resize(metadata_buffer.size() + need_dpus, metadata);

        decode_Huffman_data(header, MCU_buffer, dpu_offset);

        dpu_offset += need_dpus;
    }

    if(dpu_offset > 0) {
        nr_allocated_dpus.push_back(dpu_offset);
        metadata_buffer.resize(nr_dpus, std::vector<uint32_t>(20 + 4 * 64));
        {
            std::lock_guard<std::mutex> lock(mtx);
            batched_queue.push({filenames, MCU_buffer, metadata_buffer, nr_allocated_dpus});
        }
        cv.notify_one();
    }
    
    {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
    }
    cv.notify_one();

    clock_gettime(CLOCK_MONOTONIC, &execution_end);
    mcu_prepare_time = get_time(&execution_start, &execution_end);
}

void offloading(){
    struct timespec execution_start, execution_end;
    struct timespec tmp_start, tmp_end;

    std::vector<char> write_buffer;

    clock_gettime(CLOCK_MONOTONIC, &execution_start);
    pim.load(DPU_BINARY);
    
    while(true){
        Batch batch;
        {
            std::unique_lock<std::mutex> lock(mtx);
            
            clock_gettime(CLOCK_MONOTONIC, &tmp_start);
            cv.wait(lock, []{ return !batched_queue.empty() || finished; });
            clock_gettime(CLOCK_MONOTONIC, &tmp_end);
            total_queue_waiting_time += get_time(&tmp_start, &tmp_end);

            if (batched_queue.empty() && finished) {
                break; 
            }

            clock_gettime(CLOCK_MONOTONIC, &tmp_start);
            batch = std::move(batched_queue.front());
            batched_queue.pop();
            clock_gettime(CLOCK_MONOTONIC, &tmp_end);
            total_batch_time += get_time(&tmp_start, &tmp_end);
        }

        clock_gettime(CLOCK_MONOTONIC, &tmp_start);
        pim.copy("metadata_buffer", batch.metadata);
        pim.copy("mcus", batch.mcus);
        clock_gettime(CLOCK_MONOTONIC, &tmp_end);
        total_cpu_to_dpus_transfer_time += get_time(&tmp_start, &tmp_end);

        clock_gettime(CLOCK_MONOTONIC, &tmp_start);
        pim.exec();
        clock_gettime(CLOCK_MONOTONIC, &tmp_end);
        total_dpu_execution_time += get_time(&tmp_start, &tmp_end);
        total_offloading_times += 1;

        clock_gettime(CLOCK_MONOTONIC, &tmp_start);
        pim.copy(batch.mcus, "mcus");
        clock_gettime(CLOCK_MONOTONIC, &tmp_end);
        total_dpus_to_cpu_transfer_time += get_time(&tmp_start, &tmp_end);

        clock_gettime(CLOCK_MONOTONIC, &tmp_start);
        int dpu_offset = 0, nr_allocated_dpus = 0;
        for(int i=0; i<batch.filenames.size(); i++){
            const std::size_t pos = batch.filenames[i].find_last_of('.');
            nr_allocated_dpus = batch.nr_allocated_dpus[i];
            write_BMP(batch.metadata[dpu_offset], batch.mcus, dpu_offset, (pos == std::string::npos) ? (batch.filenames[i] + ".bmp") : (batch.filenames[i].substr(0, pos) + ".bmp"), write_buffer);
            dpu_offset += nr_allocated_dpus;
        }
        clock_gettime(CLOCK_MONOTONIC, &tmp_end);
        total_bmp_write_time += get_time(&tmp_start, &tmp_end);
    }

    clock_gettime(CLOCK_MONOTONIC, &execution_end);
    mcu_offload_time = get_time(&execution_start, &execution_end);
}

int main(int argc, char *argv[]){
    if(argc < 2){
        std::cout << "Error - Invalid arguments\n";
        return 1;
    }

    std::vector<std::string> input_files;
    for(int i=1; i<argc; i++) input_files.push_back(argv[i]);
    
    std::cout << nr_dpus << " dpus are allocated\n";

    std::thread mcu_preparer(mcu_prepare, std::ref(input_files));
    std::thread decoding_offloader(offloading);

    mcu_preparer.join();
    decoding_offloader.join();

    std::cout << "\nProfiles:\n";
    std::cout << "End-to-end execution time: " << mcu_offload_time << "s\n";

    std::cout << "Breakdowns: \n";
    std::cout << " - Queue waiting time: " << total_queue_waiting_time << "s\n";
    std::cout << " - Batch time: " << total_batch_time << "s\n";
    std::cout << " - CPU-to-DPUs transfer time: " << total_cpu_to_dpus_transfer_time << "s\n";
    std::cout << " - DPU execution time: " << total_dpu_execution_time << "s\n";
    std::cout << " - DPUs-to-CPU transfer time: " << total_dpus_to_cpu_transfer_time << "s\n";
    std::cout << " - BMP write time: " << total_bmp_write_time << "s\n";
    std::cout << " - Total offload times: " << total_offloading_times << "\n";    

    return 0;
}