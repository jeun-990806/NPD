#include <iostream>
#include <fstream>

#include "headers/bmp.h"
#include "headers/common.h"

void put_integer(std::vector<char>& buffer, const uint v){
    char temp[4] = {
        static_cast<char>((v >> 0) & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF),
        static_cast<char>((v >> 24) & 0xFF)
    };
    buffer.insert(buffer.end(), std::begin(temp), std::end(temp));
}

void put_short(std::vector<char>& buffer, const uint v){
    char temp[2] = {
        static_cast<char>((v >> 0) & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
    };
    buffer.insert(buffer.end(), std::begin(temp), std::end(temp));
}

void write_BMP(std::vector<uint32_t>& metadata, std::vector<std::vector<short>>& mcus, int start_dpu_index, const std::string& filename, std::vector<char>& write_buffer){
    std::ofstream output = std::ofstream(filename, std::ios::out | std::ios::binary);
    if(!output.is_open()){
        std::cout << filename << ": Error - Unable to create BMP file" << std::endl;
        return;
    }

    const int width = metadata[18];
    const int height = metadata[17];
    const int mcu_width = metadata[3];
    const uint padding = width % 4;
    const uint size = 14 + 12 + height * width * 3 + padding * height;
    const int max_blk_per_dpu = MAX_MCU_PER_DPU / 4;

    write_buffer.clear();

    write_buffer.push_back('B');
    write_buffer.push_back('M');
    put_integer(write_buffer, size);
    put_integer(write_buffer, 0);
    put_integer(write_buffer, 0x1A);
    put_integer(write_buffer, 12);
    put_short(write_buffer, width);
    put_short(write_buffer, height);
    put_short(write_buffer, 1);
    put_short(write_buffer, 24);

    for(int y=height-1; y>=0; y--){
        const uint mcu_row = y / 8;
        const uint pixel_row = y % 8;
        for(int x=0; x<width; x++){
            const uint mcu_column = x / 8;
            const uint pixel_column = x % 8;
            const uint pixel_index = pixel_row * 8 + pixel_column;

            int mcu_index = mcu_row * mcu_width + mcu_column;
            int block_index = (mcu_index / (mcu_width * 2)) * ((mcu_width + 1) / 2) + ((mcu_index % mcu_width) / 2);
            int block_position = (((mcu_index / mcu_width) % 2) * 2 + ((mcu_index % mcu_width) % 2)) * 64;
            int dpu_index = block_index / max_blk_per_dpu;

            block_index %= max_blk_per_dpu;
            block_index *= 768;

            char rgb[3] = {
                static_cast<char>(mcus[start_dpu_index + dpu_index][block_index + 512 + block_position + pixel_index]), // 2
                static_cast<char>(mcus[start_dpu_index + dpu_index][block_index + 256 + block_position + pixel_index]), // 1
                static_cast<char>(mcus[start_dpu_index + dpu_index][block_index + block_position + pixel_index]) // 0
            };
            write_buffer.insert(write_buffer.end(), std::begin(rgb), std::end(rgb));
        }
        write_buffer.resize(write_buffer.size() + padding, 0);
    }

    size_t chunk_size = 1024 * 1024;
    size_t written = 0;
    while (written < write_buffer.size()) {
        size_t to_write = std::min(chunk_size, write_buffer.size() - written);
        output.write(write_buffer.data() + written, to_write);
        written += to_write;
    }
    output.close();
}