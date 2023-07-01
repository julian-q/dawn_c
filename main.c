#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <glfw3webgpu.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// A simple structure holding the local information shared with the
// onAdapterRequestEnded callback.
struct AdapterUserData {
	WGPUAdapter adapter;
	bool requestEnded;
};

void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter, char const * message, void * pUserData) {
	struct AdapterUserData * userData = (struct AdapterUserData*)(pUserData);
	if (status == WGPURequestAdapterStatus_Success) {
		userData->adapter = adapter;
	} else {
        printf("Could not get WebGPU adapter: %s\n", message);
	}
	userData->requestEnded = true;
}

/**
 * Utility function to get a WebGPU adapter, so that
 *     WGPUAdapter adapter = requestAdapter(options);
 * is roughly equivalent to
 *     const adapter = await navigator.gpu.requestAdapter(options);
 */
WGPUAdapter requestAdapter(WGPUInstance instance, WGPURequestAdapterOptions const * options) {
    struct AdapterUserData userData;

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
	WGPUDevice device;
	bool requestEnded;
};

void onDeviceError(WGPUErrorType type, char const* message, void* pUserData) {
    printf("Uncaptured device error: type %u", type);
    if (message) printf(" (%s)", message);
    printf("\n");
}

void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device, char const * message, void * pUserData) { 
	struct DeviceUserData * userData = (struct DeviceUserData*)(pUserData);
	if (status == WGPURequestDeviceStatus_Success) {
		userData->device = device;
		wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, NULL /* pUserData */);
	} else {
        printf("Could not get WebGPU device: %s\n", message);
	}
	userData->requestEnded = true;
}

/**
 * Utility function to get a WebGPU device, so that
 *     WGPUAdapter device = requestDevice(adapter, options);
 * is roughly equivalent to
 *     const device = await adapter.requestDevice(descriptor);
 * It is very similar to requestAdapter
 */
WGPUDevice requestDevice(WGPUAdapter adapter, WGPUDeviceDescriptor const * descriptor) {
    struct DeviceUserData userData;

    wgpuAdapterRequestDevice(
        adapter,
        descriptor,
        onDeviceRequestEnded,
        (void*)&userData
    );

    assert(userData.requestEnded);

    return userData.device;
}

void onQueueWorkDone(WGPUQueueWorkDoneStatus status, void* pUserData) {
    printf("Queued work finished with status: %d\n", status);
}

int main (int argc, char** argv) {
    glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); 
    GLFWwindow* window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);

    WGPUInstanceDescriptor desc;
    desc.nextInChain = NULL;   
    WGPUInstance instance = wgpuCreateInstance(&desc);

	WGPUSurface surface = glfwGetWGPUSurface(instance, window);
	WGPURequestAdapterOptions adapterOpts = (WGPURequestAdapterOptions) {};
	adapterOpts.nextInChain = NULL;
	adapterOpts.compatibleSurface = surface;
	WGPUAdapter adapter = requestAdapter(instance, &adapterOpts);
    printf("Got adapter: %p\n", (void*)adapter);

	WGPUDeviceDescriptor deviceDesc = (WGPUDeviceDescriptor) {};
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

	WGPUSwapChainDescriptor swapChainDesc = (WGPUSwapChainDescriptor) {};
	swapChainDesc.nextInChain = NULL;
	swapChainDesc.width = 640;
	swapChainDesc.height = 480;

	swapChainDesc.format = WGPUTextureFormat_BGRA8Unorm;
	swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
	swapChainDesc.presentMode = WGPUPresentMode_Fifo;
	WGPUSwapChain swapChain = wgpuDeviceCreateSwapChain(device, surface, &swapChainDesc);
    printf("Swapchain: %p\n", (void*)swapChain);

	// shaderModule defined here
	const char* shaderSource = "@vertex\n\
fn vs_main(@builtin(vertex_index) in_vertex_index: u32) -> @builtin(position) vec4f {\n\
    var p = vec2f(0.0, 0.0);\n\
    if (in_vertex_index == 0u) {\n\
        p = vec2f(-0.5, -0.5);\n\
    } else if (in_vertex_index == 1u) {\n\
        p = vec2f(0.5, -0.5);\n\
    } else {\n\
        p = vec2f(0.0, 0.5);\n\
    }\n\
    return vec4f(p, 0.0, 1.0);\n\
}\n\
\n\
@fragment\n\
fn fs_main() -> @location(0) vec4f {\n\
    return vec4f(0.0, 0.4, 1.0, 1.0);\n\
}";

	WGPUShaderModuleDescriptor shaderDesc = (WGPUShaderModuleDescriptor) {};
	WGPUShaderModuleWGSLDescriptor shaderCodeDesc = (WGPUShaderModuleWGSLDescriptor) {}; 
	shaderCodeDesc.chain.next = NULL;
	shaderCodeDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
	shaderCodeDesc.source = shaderSource;
	shaderDesc.nextInChain = &shaderCodeDesc.chain;
	WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    WGPURenderPipelineDescriptor pipelineDesc = (WGPURenderPipelineDescriptor) {};
    pipelineDesc.nextInChain = NULL;

	// vertex state
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.buffers = NULL;
	pipelineDesc.vertex.module = shaderModule;
	pipelineDesc.vertex.entryPoint = "vs_main";
	pipelineDesc.vertex.constantCount = 0;
	pipelineDesc.vertex.constants = NULL;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

	// fragment state
    WGPUFragmentState fragmentState = (WGPUFragmentState) {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants = NULL;
    pipelineDesc.fragment = &fragmentState;
	pipelineDesc.depthStencil = NULL;

	// blend state
    WGPUBlendState blendState = (WGPUBlendState) {};
	blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
	blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
	blendState.color.operation = WGPUBlendOperation_Add;
	blendState.alpha.srcFactor = WGPUBlendFactor_Zero;
	blendState.alpha.dstFactor = WGPUBlendFactor_One;
	blendState.alpha.operation = WGPUBlendOperation_Add;

	// color state
    WGPUColorTargetState colorTarget = (WGPUColorTargetState) {};
	colorTarget.format = swapChainDesc.format;
	colorTarget.blend = &blendState;
	colorTarget.writeMask = WGPUColorWriteMask_All;
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	// multisampling
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;
	pipelineDesc.multisample.alphaToCoverageEnabled = false;

	pipelineDesc.layout = NULL;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);





    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
		WGPUTextureView nextTexture = wgpuSwapChainGetCurrentTextureView(swapChain);
        printf("nextTexture: %p\n", (void*)nextTexture);
		if (!nextTexture) {
            fprintf(stderr, "Cannot acquire next swap chain texture\n");
			break;
		}

		WGPUCommandEncoderDescriptor encoderDesc = (WGPUCommandEncoderDescriptor) {};
		encoderDesc.nextInChain = NULL;
		encoderDesc.label = "My command encoder";
		WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

 		WGPURenderPassColorAttachment renderPassColorAttachment = (WGPURenderPassColorAttachment) {};
 		renderPassColorAttachment.view = nextTexture;
 		renderPassColorAttachment.resolveTarget = NULL;
 		renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
 		renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
 		renderPassColorAttachment.clearValue = (WGPUColor) {0.9, 0.1, 0.2, 1.0};
 
 		WGPURenderPassDescriptor renderPassDesc = (WGPURenderPassDescriptor) {};
 		renderPassDesc.colorAttachmentCount = 1;
 		renderPassDesc.colorAttachments = &renderPassColorAttachment;
 		renderPassDesc.depthStencilAttachment = NULL;
 		renderPassDesc.timestampWriteCount = 0;
 		renderPassDesc.timestampWrites = NULL;
 		renderPassDesc.nextInChain = NULL;

 		WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

		// actually run the render pass i guess
		wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
		wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);



 		wgpuRenderPassEncoderEnd(renderPass);

		wgpuTextureViewRelease(nextTexture);

		WGPUCommandBufferDescriptor cmdBufferDescriptor = (WGPUCommandBufferDescriptor) {};
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

