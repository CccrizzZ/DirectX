//***************************************************************************************
// particleAddCSApp.cpp 
//this program has an error
//https://gist.github.com/shorttermmem/a14ab1e8980d01ac36943d0f4a8117d9
//***************************************************************************************

#include "../../Common/d3dApp.h"
#include "../../Common/UploadBuffer.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")


struct Particle
{
	XMFLOAT3 Position;
	XMFLOAT3 Velocity;
	XMFLOAT3 Acceleration;
};

class ParticleAddCSApp : public D3DApp
{
public:
    ParticleAddCSApp(HINSTANCE hInstance);
    ParticleAddCSApp(const ParticleAddCSApp& rhs) = delete;
    ParticleAddCSApp& operator=(const ParticleAddCSApp& rhs) = delete;
    ~ParticleAddCSApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

	void DoComputeWork();

	void BuildBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildPSOs();

private:

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
 
	const int NumDataElements = 16*16;

	ComPtr<ID3D12Resource> mInputBufferA = nullptr;
	ComPtr<ID3D12Resource> mInputUploadBufferA = nullptr;
	ComPtr<ID3D12Resource> mOutputBuffer = nullptr;
	ComPtr<ID3D12Resource> mOutputBuffer1 = nullptr;
	ComPtr<ID3D12Resource> mReadBackBuffer = nullptr;

};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        ParticleAddCSApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

       // return theApp.Run();
		return 0;
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

ParticleAddCSApp::ParticleAddCSApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

ParticleAddCSApp::~ParticleAddCSApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool ParticleAddCSApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));


	BuildBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

	DoComputeWork();

    return true;
}
 
void ParticleAddCSApp::OnResize()
{
}

void ParticleAddCSApp::Update(const GameTimer& gt)
{
}

void ParticleAddCSApp::Draw(const GameTimer& gt)
{
}

 
void ParticleAddCSApp::DoComputeWork()
{
	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSOs["particleAdd"].Get()));

	mCommandList->SetComputeRootSignature(mRootSignature.Get());

	mCommandList->SetComputeRootUnorderedAccessView(0, mInputBufferA->GetGPUVirtualAddress());
	mCommandList->SetComputeRootUnorderedAccessView(1, mOutputBuffer->GetGPUVirtualAddress());
 
	mCommandList->Dispatch(1, 1, 1);

	// Schedule to copy the data to the default buffer to the readback buffer.

	//D3D12_RESOURCE_STATE_COPY_SOURCE: The resource is used as the source in a copy operation. 
	//D3D12_RESOURCE_STATE_COPY_DEST: The resource is used as the destination in a copy operation.

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mOutputBuffer.Get(), 
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));

	//We need to read the data back from the GPU after computer shader does its calculation
	//mCommandList->CopyResource(Destination: CPUData, Source: GPUData);
	mCommandList->CopyResource(mReadBackBuffer.Get(), mOutputBuffer.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mOutputBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));


	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait for the work to finish.
	FlushCommandQueue();

	// Map the data so we can read it on CPU.
	Particle* mappedData = nullptr;
	ThrowIfFailed(mReadBackBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

	std::ofstream fout("results.txt");

	for(int i = 0; i < NumDataElements; ++i)
	{
		fout << "(" << mappedData[i].Acceleration.x << ", " << mappedData[i].Acceleration.y << ", " << mappedData[i].Acceleration.z <<
			", " << mappedData[i].Position.x << ", " << mappedData[i].Position.y << ", " << mappedData[i].Position.z <<
			", " << mappedData[i].Velocity.x << ", " << mappedData[i].Velocity.y << ", " << mappedData[i].Velocity.z << ")" << std::endl;
	}

	mReadBackBuffer->Unmap(0, nullptr);
}

void ParticleAddCSApp::BuildBuffers()
{
	// Generate some data.
	std::vector<Particle> dataA(NumDataElements);
	for(int i = 0; i < NumDataElements; ++i)
	{
		dataA[i].Acceleration = XMFLOAT3(1.0f, 1.0f, 1.0f);
		dataA[i].Position = XMFLOAT3((float)i, (float)i, (float)i);
		dataA[i].Velocity = XMFLOAT3(10.0f, 10.0f, 0);
	}


	std::ofstream fout("inputs.txt");
	fout << "Acceleration(x,y,z), Position(x,y,z), Velocity(x,y,z)" << std::endl;
	for (int i = 0; i < NumDataElements; ++i)
	{
		fout << "(" << dataA[i].Acceleration.x << ", " << dataA[i].Acceleration.y << ", " << dataA[i].Acceleration.z <<
			", " << dataA[i].Position.x << ", " << dataA[i].Position.y << ", " << dataA[i].Position.z <<
			", " << dataA[i].Velocity.x << ", " << dataA[i].Velocity.y << ", " << dataA[i].Velocity.z << ")" << std::endl;
	}


	UINT64 byteSize = dataA.size()*sizeof(Particle);

	// Create some buffers to be used as SRV.
	//mInputBufferA = d3dUtil::CreateDefaultBuffer(
	//	md3dDevice.Get(),
	//	mCommandList.Get(),
	//	dataA.data(),
	//	byteSize,
	//	mInputUploadBufferA);


	// Create the actual mInputBufferA buffer resource.
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mInputBufferA)));

	// In order to copy CPU memory data into our mInputBufferA buffer, we need to create
	// an intermediate upload heap. 
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(mInputUploadBufferA.GetAddressOf())));


	// Describe the data we want to copy into the mInputBufferA buffer.
	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = dataA.data();
	subResourceData.RowPitch = byteSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	// Schedule to copy the data to the mInputBufferA buffer resource.  At a high level, the helper function UpdateSubresources
	// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
	// the intermediate upload heap data will be copied to mBuffer.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mInputBufferA.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));

	UpdateSubresources(mCommandList.Get(), mInputBufferA.Get(), mInputUploadBufferA.Get(), 0, 0, 1, &subResourceData);


	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mInputBufferA.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));


	// Create the buffer that will be a UAV.
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mOutputBuffer)));
	
	//Specifies a heap used for reading back. This heap type has CPU access optimized for reading data back from the GPU, 
	//but does not experience the maximum amount of bandwidth for the GPU. This heap type is best for GPU-write-once, 
	//CPU-readable data. The CPU cache behavior is write-back, which is conducive for multiple sub-cache-line CPU reads.
	//Resources in this heap must be created with D3D12_RESOURCE_STATE_COPY_DEST, and cannot be changed away from this
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mReadBackBuffer)));
}

void ParticleAddCSApp::BuildRootSignature()
{


    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsUnorderedAccessView(0);
    slotRootParameter[1].InitAsUnorderedAccessView(1);


    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter,
		0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_NONE);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;


    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}


void ParticleAddCSApp::BuildShadersAndInputLayout()
{
	mShaders["particleAddCS"] = d3dUtil::CompileShader(L"Shaders\\ParticleAdd.hlsl", nullptr, "CS", "cs_5_1");
}

void ParticleAddCSApp::BuildPSOs()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
	computePsoDesc.pRootSignature = mRootSignature.Get();
	computePsoDesc.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["particleAddCS"]->GetBufferPointer()),
		mShaders["particleAddCS"]->GetBufferSize()
	};
	computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&mPSOs["particleAdd"])));
}



