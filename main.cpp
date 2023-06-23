#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <glfw3webgpu.h>
// #include <assert.h>
#include <stdio.h>
#include <cassert>
#include <iostream>

// A simple structure holding the local information shared with the
// onAdapterRequestEnded callback.
struct AdapterUserData {
	WGPUAdapter adapter = NULL;
	bool requestEnded = false;
};

void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * pUserData) {
	AdapterUserData& userData = *reinterpret_cast<AdapterUserData*>(pUserData);
	if (status == WGPURequestAdapterStatus_Success) {
		userData.adapter = adapter;
	} else {
        printf("Could not get WebGPU adapter: %s\n", message);
	}
	userData.requestEnded = true;
}

/**
 * Utility function to get a WebGPU adapter, so that
 *     WGPUAdapter adapter = requestAdapter(options);
 * is roughly equivalent to
 *     const adapter = await navigator.gpu.requestAdapter(options);
 */
WGPUAdapter requestAdapter(WGPUInstance instance, WGPURequestAdapterOptions const * options) {
    AdapterUserData userData;

    // Call to the WebGPU request adapter procedure
    wgpuInstanceRequestAdapter(
        instance /* equivalent of navigator.gpu */,
        options,
        onAdapterRequestEnded,
        (void*)&userData
    );

    // In theory we should wait until onAdapterReady has been called, which
    // could take some time (what the 'await' keyword does in the JavaScript
    // code). In practice, we know that when the wgpuInstanceRequestAdapter()
    // function returns its callback has been called.
    assert(userData.requestEnded);

    return userData.adapter;
}

struct DeviceUserData {
	WGPUDevice device = NULL;
	bool requestEnded = false;
};

void onDeviceError(WGPUErrorType type, char const* message, void* /* pUserData */) {
    printf("Uncaptured device error: type %u", type);
    if (message) printf(" (%s)", message);
    printf("\n");
};

void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, char const * message, void * pUserData) { 
	DeviceUserData& userData = *reinterpret_cast<DeviceUserData*>(pUserData);
	if (status == WGPURequestDeviceStatus_Success) {
		userData.device = device;
		wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, NULL /* pUserData */);
	} else {
        printf("Could not get WebGPU device: %s\n", message);
	}
	userData.requestEnded = true;
}

/**
 * Utility function to get a WebGPU device, so that
 *     WGPUAdapter device = requestDevice(adapter, options);
 * is roughly equivalent to
 *     const device = await adapter.requestDevice(descriptor);
 * It is very similar to requestAdapter
 */
WGPUDevice requestDevice(WGPUAdapter adapter, WGPUDeviceDescriptor const * descriptor) {
    DeviceUserData userData;

    wgpuAdapterRequestDevice(
        adapter,
        descriptor,
        onDeviceRequestEnded,
        (void*)&userData
    );

    assert(userData.requestEnded);

    return userData.device;
}

void onQueueWorkDone(WGPUQueueWorkDoneStatus status, void* /* pUserData */) {
    printf("Queued work finished with status: %d\n", status);
};

int main (int, char**) {
    glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); 
    GLFWwindow* window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);

    WGPUInstanceDescriptor desc = {}; // must initialize structs!!
    desc.nextInChain = NULL;   
    WGPUInstance instance = wgpuCreateInstance(&desc);

	WGPUSurface surface = glfwGetWGPUSurface(instance, window);
	WGPURequestAdapterOptions adapterOpts = {};
	adapterOpts.nextInChain = NULL;
	adapterOpts.compatibleSurface = surface;
	WGPUAdapter adapter = requestAdapter(instance, &adapterOpts);
    printf("Got adapter: %p\n", (void*)adapter);

	WGPUDeviceDescriptor deviceDesc = {};
	deviceDesc.nextInChain = NULL;
	deviceDesc.label = "My Device"; // anything works here, that's your call
	deviceDesc.requiredFeaturesCount = 0; // we do not require any specific feature
	deviceDesc.requiredLimits = NULL; // we do not require any specific limit
	deviceDesc.defaultQueue.nextInChain = NULL;
	deviceDesc.defaultQueue.label = "The default queue";
	WGPUDevice device = requestDevice(adapter, &deviceDesc);
    printf("Got device: %p\n", (void*)adapter);

	WGPUFeatureName * features;
	size_t featureCount = wgpuAdapterEnumerateFeatures(adapter, NULL);
	features = (WGPUFeatureName *)malloc(sizeof(WGPUFeatureName) * featureCount);
	wgpuAdapterEnumerateFeatures(adapter, features);
    printf("Adapter features:\n");
	for (size_t i = 0; i < featureCount; i++) {
        printf(" - %d\n", features[i]);
	}
    free(features);

	WGPUQueue queue = wgpuDeviceGetQueue(device);
	// why does below give status 3 at the end of the program?
	// maybe something to do with the fact that we must provide the extra status
	// argument in the second slot? is zero the right value?
	wgpuQueueOnSubmittedWorkDone(queue, 0, onQueueWorkDone, NULL /* pUserData */);

	WGPUSwapChainDescriptor swapChainDesc = {};
	swapChainDesc.nextInChain = NULL;
	swapChainDesc.width = 640;
	swapChainDesc.height = 480;

	swapChainDesc.format = WGPUTextureFormat_BGRA8Unorm;
	swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
	swapChainDesc.presentMode = WGPUPresentMode_Fifo;
	WGPUSwapChain swapChain = wgpuDeviceCreateSwapChain(device, surface, &swapChainDesc);
    printf("Swapchain: %p\n", (void*)swapChain);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
		WGPUTextureView nextTexture = wgpuSwapChainGetCurrentTextureView(swapChain);
        printf("nextTexture: %p\n", (void*)nextTexture);
		if (!nextTexture) {
            fprintf(stderr, "Cannot acquire next swap chain texture\n");
			break;
		}

		WGPUCommandEncoderDescriptor encoderDesc = {};
		encoderDesc.nextInChain = NULL;
		encoderDesc.label = "My command encoder";
		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

 		WGPURenderPassColorAttachment renderPassColorAttachment = {};
 		renderPassColorAttachment.view = nextTexture;
 		renderPassColorAttachment.resolveTarget = NULL;
 		renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
 		renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
 		renderPassColorAttachment.clearValue = WGPUColor{0.9, 0.1, 0.2, 1.0};
 
 		WGPURenderPassDescriptor renderPassDesc = {};
 		renderPassDesc.colorAttachmentCount = 1;
 		renderPassDesc.colorAttachments = &renderPassColorAttachment;
 		renderPassDesc.depthStencilAttachment = NULL;
 		renderPassDesc.timestampWriteCount = 0;
 		renderPassDesc.timestampWrites = NULL;
 		renderPassDesc.nextInChain = NULL;

 		WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
 		wgpuRenderPassEncoderEnd(renderPass);

		wgpuTextureViewRelease(nextTexture);

		WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
		cmdBufferDescriptor.nextInChain = NULL;
		cmdBufferDescriptor.label = "Command buffer";
		WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDescriptor);
		wgpuQueueSubmit(queue, 1, &command);

		wgpuSwapChainPresent(swapChain);
    }

	wgpuSwapChainRelease(swapChain);
	wgpuDeviceRelease(device);
	wgpuAdapterRelease(adapter);
	wgpuInstanceRelease(instance);
	wgpuSurfaceRelease(surface);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

