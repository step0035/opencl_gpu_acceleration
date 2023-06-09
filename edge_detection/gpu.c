#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <CL/cl.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define WGSIZE 16
#define PROGRAM_FILE "sobel.cl"
#define KERNEL_NAME "sobel"

int main(int argc, char** argv)
{
    struct timeval start_time;
    struct timeval end_time;

    if (argc != 2)
    {
        printf("Usage: %s <input image>\n", argv[0]);
        return -1;
    }

    int width;
    int height;
    int channels;
    unsigned char* input_data = stbi_load(argv[1], &width, &height, &channels, STBI_grey);
    if (!input_data)
    {
        printf("Failed to load input image\n");
        return -1;
    }

    // we want to preserve the original iamage size, so we add +1 padding around the input image

    printf("input_width : %d\r\n", width);
    printf("input_height : %d\r\n", height);

    // note that below malloc may fail if image size is too big due to heap size limitation
    unsigned char* output_data = (unsigned char*) malloc((width) * (height));

    cl_int error;
    cl_uint num_platforms;
    cl_platform_id platform;

    // get platform
    error = clGetPlatformIDs(1, &platform, &num_platforms);
    if (error != CL_SUCCESS || num_platforms == 0)
    {
        printf("Failed to find OpenCL platform\n");
        return -1;
    }

    // get device
    cl_device_id device;
    error = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
    if (error != CL_SUCCESS)
    {
        printf("Failed to find OpenCL GPU device\n");
        return -1;
    }

    // create context
    cl_context context = clCreateContext(NULL, 1, &device, NULL, NULL, &error);
    if (error != CL_SUCCESS)
    {
        printf("Failed to create OpenCL context\n");
        return -1;
    }

    // create command queue
    // cl_command_queue_properties property = CL_QUEUE_PROFILING_ENABLE; // enable profiling
    cl_queue_properties properties[] = {CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0};
    cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, &properties[0], &error);
    if (error != CL_SUCCESS)
    {
        printf("error: %d\n", error);
        printf("Failed to create OpenCL command queue\n");
        return -1;
    }

    // create some cl events for profiling
    cl_event write_timing_event;
    cl_event read_timing_event;
    cl_ulong time_start;
    cl_ulong time_end;
    cl_ulong write_time;
    cl_ulong read_time;

    // set input and output image format
    cl_image_format format;
    format.image_channel_order = CL_R;
    format.image_channel_data_type = CL_UNORM_INT8;

    // input image descriptor
    cl_image_desc clImageDescInput;
    clImageDescInput.image_type = CL_MEM_OBJECT_IMAGE2D;
    clImageDescInput.image_width = width;
    clImageDescInput.image_height = height;
    clImageDescInput.image_row_pitch = 0;
    clImageDescInput.image_slice_pitch = 0;
    clImageDescInput.num_mip_levels = 0;
    clImageDescInput.num_samples = 0;
    clImageDescInput.buffer = NULL;

    // output image descriptor
    cl_image_desc clImageDescOutput;
    clImageDescOutput.image_type = CL_MEM_OBJECT_IMAGE2D;
    clImageDescOutput.image_width = width;
    clImageDescOutput.image_height = height;
    clImageDescOutput.image_row_pitch = 0;
    clImageDescOutput.image_slice_pitch = 0;
    clImageDescOutput.num_mip_levels = 0;
    clImageDescOutput.num_samples = 0;
    clImageDescOutput.buffer = NULL;

    // create input image buffer
    cl_mem input_image = clCreateImage(context, CL_MEM_READ_ONLY, &format, &clImageDescInput, NULL, &error);
    if (error != CL_SUCCESS)
    {
        printf("Failed to create input image buffer\n");
        return -1;
    }

    // create output image buffer
    cl_mem output_image = clCreateImage(context, CL_MEM_WRITE_ONLY, &format, &clImageDescOutput, NULL, &error);
    if (error != CL_SUCCESS)
    {
        printf("Failed to create output image buffer\n");
        return -1;
    }

    // copy input image data to opencl input image buffer
    size_t origin[3] = {0, 0, 0};
    size_t region[3] = {width, height, 1};
    error = clEnqueueWriteImage(queue, input_image, CL_TRUE, origin, region, 0, 0, input_data, 0, NULL, &write_timing_event);
    if (error != CL_SUCCESS)
    {
        printf("error: %d\r\n", error);
        printf("Failed to write input image data to input image buffer\n");
        return -1;
    }

    clGetEventProfilingInfo(write_timing_event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
    clGetEventProfilingInfo(write_timing_event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL); 

    write_time = time_end - time_start;
    printf("write time: %ld\n", write_time);

    // Create the compute program from the .cl file
    // first we read the kernel code string from the file
    FILE *fhandle;
    size_t fsize;
    char *fbuffer;
    fhandle = fopen(PROGRAM_FILE, "rb");
    if (fhandle == NULL)
    {
        perror("Couldn't open the program file");
        exit(1);
    }
    fseek(fhandle, 0, SEEK_END);
    fsize = ftell(fhandle);
    rewind(fhandle);
    fbuffer = (char *) malloc(fsize + 1);
    fbuffer[fsize] = '\0';
    fread(fbuffer, sizeof(char), fsize, fhandle);
    fclose(fhandle);

    // Create OpenCL program and kernel
    cl_program program = clCreateProgramWithSource(context, 1, (const char **)&fbuffer, NULL, &error);
    if (error != CL_SUCCESS)
    {
        printf("Failed to create OpenCL program\n");
        return -1;
    }

    error = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
    if (error != CL_SUCCESS)
    {
        printf("Failed to build OpenCL program\n");
        char* build_log;
        size_t log_size;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
        build_log = (char*) malloc(log_size + 1);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, build_log, NULL);
        build_log[log_size] = '\0';
        printf("%s\n", build_log);
        free(build_log);
        return -1;
    }

    cl_kernel kernel = clCreateKernel(program, KERNEL_NAME, &error);
    if (error != CL_SUCCESS)
    {
        printf("Failed to create OpenCL kernel\n");
        return -1;
    }

    // map the input arguments of the kernel
    error = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input_image);
    error |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output_image);
    error |= clSetKernelArg(kernel, 2, sizeof(int), &width);
    error |= clSetKernelArg(kernel, 3, sizeof(int), &height);
    if (error != CL_SUCCESS)
    {
        printf("Failed to set OpenCL kernel arguments\n");
        return -1;
    }

    // global workgroup size is the output image size = width * height
    size_t globalws[2] = {width, height};
    // local workgroup size, if optimzied, can handle up to 16x16 work item
    size_t localws[2] = {WGSIZE, WGSIZE};

    // get start_time
    gettimeofday(&start_time, NULL);

    // Execute kernel
    error = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, globalws, localws, 0, NULL, NULL);
    if (error != CL_SUCCESS)
    {
        printf("Failed to execute OpenCL kernel\n");
        return -1;
    }

    clFlush(queue);

    // Wait for the command queue to get serviced before reading back results
    clFinish(queue);

    // get end_time
    gettimeofday(&end_time, NULL);

    double elapsed_time = (end_time.tv_sec - start_time.tv_sec) + (double)(end_time.tv_usec - start_time.tv_usec)/1000000.0;
 
    // Read output image data from output image buffer
    error = clEnqueueReadImage(queue, output_image, CL_TRUE, origin, region, 0, 0, output_data, 0, NULL, &read_timing_event);
    if (error != CL_SUCCESS)
    {
        printf("%d\r\n", error);
        printf("Failed to read output image data from output image buffer\n");
        return -1;
    }

    clGetEventProfilingInfo(read_timing_event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
    clGetEventProfilingInfo(read_timing_event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL); 
    
    read_time = time_end - time_start;
    printf("read time: %ld\n", read_time);

    // write the output image
    stbi_write_jpg("output_gpu.jpg", width, height, 1, output_data, 100);

    // Cleanup
    clReleaseMemObject(input_image);
    clReleaseMemObject(output_image);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    free(output_data);

    printf("Time taken = %f seconds\r\n", elapsed_time);
    printf("overhead time: %ld ms\n", (write_time + read_time)/1000);

    return 0;
}
