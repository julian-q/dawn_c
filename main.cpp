#include <iostream>
#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <cassert>
#include <glfw3webgpu.h>
#include <vector>


/**
 * Utility function to get a WebGPU adapter, so that
 *     WGPUAdapter adapter = requestAdapter(options);
 * is roughly equivalent to
 *     const adapter = await navigator.gpu.requestAdapter(options);
 */
WGPUAdapter requestAdapter(WGPUInstance instance, WGPURequestAdapterOptions const * options) {
    // A simple structure holding the local information shared with the
    // onAdapterRequestEnded callback.
    struct UserData {
        WGPUAdapter adapter = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    // Callback called by wgpuInstanceRequestAdapter when the request returns
    // This is a C++ lambda function, but could be any function defined in the
    // global scope. It must be non-capturing (the brackets [] are empty) so
    // that it behaves like a regular C function pointer, which is what
    // wgpuInstanceRequestAdapter expects (WebGPU being a C API). The workaround
    // is to convey what we want to capture through the pUserData pointer,
    // provided as the last argument of wgpuInstanceRequestAdapter and received
    // by the callback as its last argument.
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * pUserData) {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestAdapterStatus_Success) {
            userData.adapter = adapter;
        } else {
            std::cout << "Could not get WebGPU adapter: " << message << std::endl;
        }
        userData.requestEnded = true;
    };

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

/**
 * Utility function to get a WebGPU device, so that
 *     WGPUAdapter device = requestDevice(adapter, options);
 * is roughly equivalent to
 *     const device = await adapter.requestDevice(descriptor);
 * It is very similar to requestAdapter
 */
WGPUDevice requestDevice(WGPUAdapter adapter, WGPUDeviceDescriptor const * descriptor) {
    struct UserData {
        WGPUDevice device = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status, WGPUDevice device, char const * message, void * pUserData) {
        UserData& userData = *reinterpret_cast<UserData*>(pUserData);
        if (status == WGPURequestDeviceStatus_Success) {
            userData.device = device;
			auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
				std::cout << "Uncaptured device error: type " << type;
				if (message) std::cout << " (" << message << ")";
				std::cout << std::endl;
			};
			wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);
        } else {
            std::cout << "Could not get WebGPU device: " << message << std::endl;
        }
        userData.requestEnded = true;
    };

    wgpuAdapterRequestDevice(
        adapter,
        descriptor,
        onDeviceRequestEnded,
        (void*)&userData
    );

    assert(userData.requestEnded);

    return userData.device;
}

int main (int, char**) {
    glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); 
    GLFWwindow* window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);

    WGPUInstanceDescriptor desc = {}; // must initialize structs!!
    desc.nextInChain = nullptr;   
    WGPUInstance instance = wgpuCreateInstance(&desc);

	WGPUSurface surface = glfwGetWGPUSurface(instance, window);
	WGPURequestAdapterOptions adapterOpts = {};
	adapterOpts.nextInChain = nullptr;
	adapterOpts.compatibleSurface = surface;
	WGPUAdapter adapter = requestAdapter(instance, &adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;

	WGPUDeviceDescriptor deviceDesc = {};
	deviceDesc.nextInChain = nullptr;
	deviceDesc.label = "My Device"; // anything works here, that's your call
	deviceDesc.requiredFeaturesCount = 0; // we do not require any specific feature
	deviceDesc.requiredLimits = nullptr; // we do not require any specific limit
	deviceDesc.defaultQueue.nextInChain = nullptr;
	deviceDesc.defaultQueue.label = "The default queue";
	WGPUDevice device = requestDevice(adapter, &deviceDesc);
	std::cout << "Got device: " << device << std::endl;

	std::vector<WGPUFeatureName> features;
	size_t featureCount = wgpuAdapterEnumerateFeatures(adapter, nullptr);
	features.resize(featureCount);
	wgpuAdapterEnumerateFeatures(adapter, features.data());
	std::cout << "Adapter features:" << std::endl;
	for (auto f : features) {
		std::cout << " - " << f << std::endl;
	}

	WGPUQueue queue = wgpuDeviceGetQueue(device);
	auto onQueueWorkDone = [](WGPUQueueWorkDoneStatus status, void* /* pUserData */) {
		std::cout << "Queued work finished with status: " << status << std::endl;
	};
	// why does below give status 3 at the end of the program?
	// maybe something to do with the fact that we must provide the extra status
	// argument in the second slot? is zero the right value?
	wgpuQueueOnSubmittedWorkDone(queue, 0, onQueueWorkDone, nullptr /* pUserData */);

	WGPUSwapChainDescriptor swapChainDesc = {};
	swapChainDesc.nextInChain = nullptr;
	swapChainDesc.width = 640;
	swapChainDesc.height = 480;

	swapChainDesc.format = WGPUTextureFormat_BGRA8Unorm;
	swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
	swapChainDesc.presentMode = WGPUPresentMode_Fifo;
	WGPUSwapChain swapChain = wgpuDeviceCreateSwapChain(device, surface, &swapChainDesc);
	std::cout << "Swapchain: " << swapChain << std::endl;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
		WGPUTextureView nextTexture = wgpuSwapChainGetCurrentTextureView(swapChain);
		std::cout << "nextTexture: " << nextTexture << std::endl;
		if (!nextTexture) {
			std::cerr << "Cannot acquire next swap chain texture" << std::endl;
			break;
		}

		WGPUCommandEncoderDescriptor encoderDesc = {};
		encoderDesc.nextInChain = nullptr;
		encoderDesc.label = "My command encoder";
		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

 		WGPURenderPassColorAttachment renderPassColorAttachment = {};
 		renderPassColorAttachment.view = nextTexture;
 		renderPassColorAttachment.resolveTarget = nullptr;
 		renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
 		renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
 		renderPassColorAttachment.clearValue = WGPUColor{0.9, 0.1, 0.2, 1.0};
 
 		WGPURenderPassDescriptor renderPassDesc = {};
 		renderPassDesc.colorAttachmentCount = 1;
 		renderPassDesc.colorAttachments = &renderPassColorAttachment;
 		renderPassDesc.depthStencilAttachment = nullptr;
 		renderPassDesc.timestampWriteCount = 0;
 		renderPassDesc.timestampWrites = nullptr;
 		renderPassDesc.nextInChain = nullptr;

 		WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
 		wgpuRenderPassEncoderEnd(renderPass);

		wgpuTextureViewRelease(nextTexture);

		WGPUCommandBufferDescriptor cmdBufferDescriptor = {};
		cmdBufferDescriptor.nextInChain = nullptr;
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

