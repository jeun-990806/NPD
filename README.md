# NPD (Novel PIM Decoder)
## Build
```
git clone https://github.com/jeun-990806/NPD.git
cd NPD
make
```

### Modify the Makefile
* Line 1: `NUM_TASKLETS` — Set the number of tasklets.
* Line 2: `MAX_MCU_PER_DPU` — Set the number of MCUs processed by each DPU.

## Convert JPEG Images to BMP
```
./bin/decoder <jpeg_image_1> ...
```
* Output BMP files will be generated in the same directory as the input JPEG images.
* The output file names will follow the format of the input files, with .bmp extensions.

## Performance Profiling
After execution, a performance profile will be printed to the console. Example:

```
Profiles:
End-to-end execution time: 0.282225s
Breakdowns: 
 - Queue waiting time: 0.109344s
 - CPU-to-DPUs transfer time: 0.0377356s
 - DPU execution time: 0.0288185s
 - DPUs-to-CPU transfer time: 0.0408663s
 - BMP write time: 0.0100065s
 - Total offload times: 1
```

## Citation
```
@conference{npd_ipdps25,
  title   = {{Enhanced JPEG Decoding using PIM Architectures with Parallel MCU Processing}},
  author  = {Jieun Kim and Dukyun Nam},
  booktitle = {Proceedings of the IEEE International Parallel & Distributed Processing Symposium},
  year    = {2025},
  month   = {June}
}
```
