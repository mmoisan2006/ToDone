//Just ripped the Dear ImGUi example for dx12
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include "main.h"
#include <vector>
#include <map>
#include <string>
#include <ctime>
#include <cstdlib>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include "ImGuiDatePicker.hpp"

#pragma comment(lib, "Comdlg32.lib") // Links the Windows dialog library

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

// Config for example app
static const int APP_NUM_FRAMES_IN_FLIGHT = 2;
static const int APP_NUM_BACK_BUFFERS = 2;
static const int APP_SRV_HEAP_SIZE = 64;

struct FrameContext
{
    ID3D12CommandAllocator*     CommandAllocator;
    UINT64                      FenceValue;
};

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap*       Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT                        HeapHandleIncrement;
    ImVector<int>               FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
    {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back(n - 1);
    }
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

// Data
static FrameContext                 g_frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static ID3D12Device*                g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap*        g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap*        g_pd3dSrvDescHeap = nullptr;
static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
static ID3D12CommandQueue*          g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList*   g_pd3dCommandList = nullptr;
static ID3D12Fence*                 g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3*             g_pSwapChain = nullptr;
static bool                         g_SwapChainTearingSupport = false;
static bool                         g_SwapChainOccluded = false;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource*              g_mainRenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForPendingOperations();
FrameContext* WaitForNextFrameContext();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//Need these as globals for auto-saving
std::vector <task> taskList;
std::vector <category> categories;
std::vector <task> completedTaskList;

std::map<int, std::vector<task>> completedWeekBuckets;
std::map<int, std::vector<task>> weekBuckets;

char autosavePath[260] = "";
char lastUsedPath[260] = "";


// Main code
int main(int, char**)
{
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"To-Do", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"To-Do", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = g_pd3dDevice;
    init_info.CommandQueue = g_pd3dCommandQueue;
    init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
    // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
    init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)            { return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle); };
    ImGui_ImplDX12_Init(&init_info);

    // Before 1.91.6: our signature was using a single descriptor. From 1.92, specifying SrvDescriptorAllocFn/SrvDescriptorFreeFn will be required to benefit from new features.
    //ImGui_ImplDX12_Init(g_pd3dDevice, APP_NUM_FRAMES_IN_FLIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, g_pd3dSrvDescHeap, g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(), g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    //Stuff that Actually matters for what im doing

    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    bool var = false;
    static bool openCategoryPopup = false;
    static char taskName[50] = "Enter Task";
    static char categoryName[50] = "Enter Catergory";
    char buffer[50];

    //Seed random or some fun stuff
    srand((unsigned int)time(nullptr));

    //Flag for displaying unsaved icon in tab
    static bool saved = false;


    //Handles keeping up to date record of what time it is
    // 1. Get the current time in seconds
    std::time_t t = std::time(nullptr);
    // 2. Convert those seconds into the tm struct format
    struct tm dueDate {};
    //Saves to due date so we can use
    localtime_s(&dueDate, &t);


    struct tm endDate = dueDate;

    //Common colorss
    ImVec4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    static ImVec4 color = defaultColor; // Default Color

    //Color gradient that are re-used
    ImVec4 darkRedColor = ImVec4(0.6f, 0.0f, 0.0f, 1.0f);
    ImVec4  redColor = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // red
    ImVec4 yellowColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f); // yellow
    ImVec4 greenColor = ImVec4(0.2f, 0.8f, 0.4f, 1.0f); // green



    //autsave path fixed with formatting accurate to any system
    GetModuleFileNameA(nullptr, autosavePath, sizeof(autosavePath));
    char* lastSlash = strrchr(autosavePath, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
    }
    strcat(autosavePath, "..\\saves\\autosave\\autosave.txt");
    //auto-saving timers
    // Add with your other statics inside the loop
    static int autoSaveCounter = 0;
    static int autoSaveMin = 10;
    int autoSaveInterval = 3600*autoSaveMin; // at 60 frames per second over 60 seconds gives us a minute so 5 minute is the interval of auto-saves

    //Load config to see if any file was used more recently
    loadConfig(lastUsedPath, sizeof(lastUsedPath));

    if (strlen(lastUsedPath) > 0) {
        openFile(taskList, categories, completedTaskList, lastUsedPath);
    }
    else {
        openFile(taskList, categories, completedTaskList, autosavePath);
    }

    //Ensures we always have atleast generel
    if (categories.empty()) {
        categories.push_back({ "General", 0, defaultColor });
    }

    //windowFlags to display not saved
    ImGuiWindowFlags windowFlags;

    ///////////////////////////////////////////////// WHILE LOOP //////////////////////////////////////////////////////////////////////////////
    while (!done)
    {
        //This is responsible for auto-saving every interval of time chosen
        autoSaveCounter++;

        if (autoSaveCounter >= autoSaveInterval) {
            saveFile(taskList, categories, completedTaskList, getSavePath());
            autoSaveCounter = 0;
            saved = true;
        }

        if (!saved) {
            windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_UnsavedDocument;
        }
        else{
            windowFlags = ImGuiWindowFlags_MenuBar;
        }

        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window screen locked
        if ((g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) || ::IsIconic(hwnd))
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();


        //All variables needed inside of the while loop that re-initialize
        static int priority = 50;
        static int catPriority = 0;
        static int selected_idx = 0;
        static int rightClicked_idxTask = 0;
        static int rightClicked_idxCat = 0;
        static int done_idx = 0;
        static int undone_idx = 0;
        static int repeat_idx = 0;
        static int repeatInterval = 1;
        static int selectedWeek = 0;


        //Static basically created a variable that persists through loops and doset have its value reset every time(For classes it shares across all children in class)
        static bool taskModalOpen = false;
        static bool taskPopUpOpen = false;
        static bool categoryModalOpen = false;
        static bool drawTasksToDo = false;
        static bool drawTasksDone = false;
        static bool show_demo_window = false;
        static bool catNeedSort = true;
        static bool taskNeedSort = true;
        static bool completedTaskNeedSort = true;
        static bool showCantDelete = false;
        static bool showCantDeleteTask = false;
        static bool isEditingTask = false;
        static bool isEditingCat = false;
        static bool isCompleted = false;
        static bool openTaskEdit = false;
        static bool taskDone = false;
        static bool taskUndone = false;
        static bool repeatForever = true;
        static bool hasCopies = false;
        static bool drawCompletion = false;
        static bool drawCalendar = false;
        static bool weekBucketsDirty = true;
   
        //////////////////////////////////////// MENU BAR/////////////////////////////////////////////////////////////////

        //Beggining the menu bar for user experience
        ImGui::BeginMainMenuBar();

        //All File management user would do
        if (ImGui::BeginMenu("File")) {

            //We need to make a copy to prevent actaully modifying last path used due to pointers
            char displayName[260];
            strncpy(displayName, lastUsedPath, sizeof(displayName) - 1);
            displayName[sizeof(displayName) - 1] = '\0';

            //Find last instance of \ which in c is \\ for some reason
            char* fileName = strrchr(displayName, '\\');
            fileName = fileName ? fileName + 1 : displayName;
            //Just a safety thing here tat if there is a file name we just advance file name by 1 i.e after the \, otherwise we stick to display name

            //We find the . part of the .txt and replace it with the null terminiating char \0
            char* dot = strrchr(fileName, '.');
            if (dot) *dot = '\0';

            ImGui::Text(fileName);

            ImGui::Separator();

            //Manages new file creation
            if (ImGui::MenuItem("New")) {
                std::string path = GetPathFromUser(true);
                newfile(taskList, categories, completedTaskList, path.c_str());
                strncpy(lastUsedPath, path.c_str(), sizeof(lastUsedPath) - 1);
                lastUsedPath[sizeof(lastUsedPath) - 1] = '\0';
                saveConfig(lastUsedPath);
                saved = true;
                weekBucketsDirty = true;
            }


            if (ImGui::MenuItem("Open")) {
                //Uses helper functions to get path from user then open file selected
                std::string path = GetPathFromUser(false);
                if (!path.empty()) {
                    openFile(taskList, categories, completedTaskList, path.c_str());
                    strncpy(lastUsedPath, path.c_str(), sizeof(lastUsedPath)-1);
                    lastUsedPath[sizeof(lastUsedPath) - 1] = '\0';
                    saveConfig(lastUsedPath);
                    saved = true;
                    weekBucketsDirty = true;
                }
                
            }


            if (ImGui::MenuItem("Save")) {
                //Uses helper function to get path from user then to save file
                std::string path = GetPathFromUser(true);
                if (!path.empty()) {
                    saveFile(taskList, categories, completedTaskList, path.c_str());
                    saveConfig(lastUsedPath);
                    strncpy(lastUsedPath, path.c_str(), sizeof(lastUsedPath)-1);
                    lastUsedPath[sizeof(lastUsedPath) - 1] = '\0';
                    saved = true;
                    weekBucketsDirty = true;
                }

            }

            if (ImGui::MenuItem("Exit")) {
                //Makes sure the user wants to quit then quits the program
                saveFile(taskList, categories, completedTaskList, getSavePath());
                done = true;
            }

            ImGui::EndMenu();
        }


        //View allows the user to manage exaclty what they want to see
        if (ImGui::BeginMenu("View")) {
            //When clicked draws the task window for the user to mess around with
            if (ImGui::MenuItem("Tasks To-Do")) {
                drawTasksToDo = true;
            }

            if (ImGui::MenuItem("Tasks Done")) {
                drawTasksDone = true;
            }

            if (ImGui::MenuItem("Fancy Completion Tracker")) {
                drawCompletion = true;
            }

            if (ImGui::MenuItem("Calendar")) {
                drawCalendar = true;
            }

            ImGui::EndMenu();
        }


        //Handles Users wanted to customize ui
        if (ImGui::BeginMenu("Profile")) {
            if (ImGui::MenuItem("Appearance")){

            }

            if (ImGui::MenuItem("Settings")) {

            }
            ImGui::EndMenu();
        }


        if (ImGui::BeginMenu("DEV")) {
            //Stuff for me to be able to check stuff and for advanced messing around with settings ig
            if (ImGui::MenuItem("Demo")) {
                show_demo_window = true;
            }

            if (ImGui::MenuItem("Ex1")) {
                //Catagory creation for test
                taskList.clear();
                completedTaskList.clear();
                categories.clear();
                
                categories.push_back({ "General", 0, defaultColor });
                for (int i = 0; i < 5; i++) {
                    category c = {};
                    std::string name = "Cat" + std::to_string(i);
                    strncpy(c.name, name.c_str(), sizeof(c.name));
                    c.name[sizeof(c.name)-1] = '\0';
                    c.priority = rand() % 10 + 1; //Gives a random priority
                    c.color.x = (rand() % 255)/255.0f; // since rgb are expected floats from 0.0-1.0 just with 255 optons
                    c.color.y = (rand() % 255)/255.0f;
                    c.color.z = (rand() % 255)/255.0f;
                    c.color.w = (rand() % 255) / 255.0f;
                    categories.push_back(c);
                }

                catNeedSort = true;

                //Random Task creation
                for (int i = 0; i < rand() % 20 +1; i++) {
                    task t = {};
                    std::string name = "Task" + std::to_string(i);
                    strncpy(t.name, name.c_str(), sizeof(t.name)-1);
                    t.name[sizeof(t.name) - 1] = '\0';
                    t.priority = rand() % 100 + 1; // Generates a random number from 1-100
                    t.completed = false;
                    t.dueDate.tm_year = rand() % 5 + 126; // From 1900 therefore to get to 2026 we add 126
                    t.dueDate.tm_mon = rand() % 12; // Remember rand % is not inclusive and tm mon works 0-11
                    t.dueDate.tm_mday = rand() % 28 + 1;
                    t.dueDate.tm_hour = rand() % 24;
                    t.dueDate.tm_min = rand() % 60;
                    t.cat = categories[rand() % 5]; //Random category 1-5
                    taskList.push_back(t);
                }

                for (int i = 0; i < rand() % 10 + 1; i++) {
                    task t = {};
                    std::string name = "Completed Task" + std::to_string(i);
                    strncpy(t.name, name.c_str(), sizeof(t.name) - 1);
                    t.name[sizeof(t.name) - 1] = '\0';
                    t.priority = rand() % 100 + 1; // Generates a random number from 1-100
                    t.completed = true;
                    t.dueDate.tm_year = rand() % 5 + 126; // From 1900 therefore to get to 2026 we add 126
                    t.dueDate.tm_mon = rand() % 12; // Remember rand % is not inclusive and tm mon works 0-11
                    t.dueDate.tm_mday = rand() % 28 + 1;
                    t.dueDate.tm_hour = rand() % 24;
                    t.dueDate.tm_min = rand() % 60;
                    t.cat = categories[rand() % 5]; //Random category 1-5
                    completedTaskList.push_back(t);
                }
            }

            ImGui::EndMenu();
        }

        //Demo window for debugging
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window) {
            ImGui::ShowDemoWindow(&show_demo_window);
            //quite generally helpful to have when debugging
        }

        
        ImGui::EndMainMenuBar();

        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S)) {
            saveFile(taskList, categories, completedTaskList, getSavePath());
            saved = true;
        }
        ////////////////////////////////    TASK TO-DO WINDOW     ////////////////////////////////////////////

        //Flags for table
        ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Sortable |
            ImGuiTableFlags_SortMulti |
            ImGuiTableFlags_SortTristate | // We want to use tristate since it lets up have three modes which enables dragging and sorting together!(enable option to unclick sorting)
            ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_BordersV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_Hideable;

        //Enables only when selected in view menu and with a conditional to allow closing/re-opening
        if (drawTasksToDo) {
            ImGui::Begin("Tasks To-Do", &drawTasksToDo, windowFlags);//Flag needed for new task to be in the menu bar

            if (ImGui::BeginTable("Tasks", 5, tableFlags)) {

                //Headers for table
                //We setup up in column style since we want to enable sorting in the flags and are unable to do it this way otherwise
                ImGui::TableSetupColumn("Done", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
                ImGui::TableSetupColumn("Difficulty", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Due Date", ImGuiTableColumnFlags_PreferSortAscending);
                ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_None);
                ImGui::TableHeadersRow(); // Renders the header row with sort arrows automatically

                //Sorting logic for table that is called appropriatly

                if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                    //ImGui has a crap ton of logic for spec sorting already so were gonna mainly use that for sorting where we can
                    taskNeedSort = sortSpecs->SpecsCount > 0;

                    if (sortSpecs->SpecsDirty) {
                        //Basically marks when something caused table to become unsorted(which is true on first frame)
                        

                        //You can shift click to sort multiple columns at once so we need to loop through them to account for that(Its what specsCount Tracks)
                        for (int n = 0; n < sortSpecs->SpecsCount;n++) {
                            //Create a nice variable to accces a single spec
                            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[n];

                            //Check if the arrow is ascending or descending and save it as a bool
                            bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

                            //Now depending on which column it is we need to different things so we do a simple switch case

                            switch (spec.ColumnIndex) {
                                case 1://Name
                                    //Stable sort takes (begin, end, comparison) whith compariso deciding how we decide the order
                                    std::stable_sort(taskList.begin(), taskList.end(),
                                        //Capture ascneding so its usable in the stable sort comparison
                                        [ascending](const task& a, const task& b) {
                                            //Runs comparison function from string which returns greater then 0 if a first char and so on is greater then b char
                                            int cmp = strcmp(a.name, b.name);
                                            //if ascending is true we return the result of comparison < 0 i.e a is less then b(i.e true for ascending)
                                            //else it returns false meaning while a is less then b we actaully want higher cmp first
                                            return ascending ? cmp < 0 : cmp > 0;
                                        }
                                        //Stable sort itself handles the re-ordering through internal calls and pass by references for us
                                    );
                                    //End of switch case
                                    break;


                                case 2: //Priority
                                    std::stable_sort(taskList.begin(), taskList.end(), [ascending](const task& a, const task& b) {
                                        //We basically just return that true if for ascneding a is greater then b and vice versa if descending
                                        return ascending ? a.priority > b.priority : a.priority < b.priority;
                                        }
                                    );

                                    break;

                                case 3:  //Due Date
                                    std::stable_sort(taskList.begin(), taskList.end(), [ascending](const task& a, const task& b) {
                                        //Converts tm to time_t which is just a single large integer making comparison easy
                                        //mktime just handles the breaking down of tm to time_t as a function
                                        struct tm tempA = a.dueDate;
                                        struct tm tempB = b.dueDate;
                                        tempA.tm_isdst = -1;
                                        tempB.tm_isdst = -1;
                                        time_t ta = mktime(&tempA);
                                        time_t tb = mktime(&tempB);
                                        return ascending ? ta > tb : ta < tb;
                                        });
                                    break;

                                case 4: //Category
                                    std::stable_sort(taskList.begin(), taskList.end(),
                                        [ascending](const task& a, const task& b) {
                                            int cmp = strcmp(a.cat.name, b.cat.name);
                                            return ascending ? cmp < 0 : cmp > 0;
                                        }
                                        //Stable sort itself handles the re-ordering through internal calls and pass by references for us
                                    );
                                    break;
                            }


                        }
                        //Marks that we finished sorting so no sorting logic needs to be run
                        sortSpecs->SpecsDirty = false;
                    }

                }


                //Checks if its properly sorted which will be used later to prevent dragging behaviour from occuring
                for (int row = 0; row < taskList.size(); row++) {
                    //Creates a row
                    ImGui::TableNextRow();
                    ImGui::PushID(row);

                    //First element in index is a checkbox to complete task
                    ImGui::TableSetColumnIndex(0);
                    //ImGui::Checkbox("##compelte", &taskList[row].completed);
                    if (ImGui::Checkbox("##complete", &taskList[row].completed)){
                        //Just update that something got completed
                        saved = false;
                        taskList[row].completed = true;
                        completedTaskList.push_back(taskList[row]);

                        //Marks for the task to be now deleted form the table after we saved the important info
                        taskDone = true;
                        done_idx = row;
                        taskNeedSort = true;
                        weekBucketsDirty = true;
                    }

                    ////Allows for use to delete item removing it form categories
                    //if (ImGui::MenuItem("Delete")) {
                    //    taskList.erase(taskList.begin() + rightClicked_idx);
                    //    //Needs to resort priorities
                    //    taskNeedSort = true;

                    //Second is its name
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Selectable(taskList[row].name, false, ImGuiSelectableFlags_SpanAllColumns);


                    ///Removing/Editing Behaviour
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        rightClicked_idxTask = row;
                        openTaskEdit = true;
                    }

                    //Manages drag and drop behaviour of task(must be right after selectable for ImGui to not crash)
                    //Creates source TASK_Row for drag and drop which gives the target the row of whats being dragged/dropped

                    //Is disabled when the table is unsorted as it would cause many errors due to conflicting and changing order !isSorted
                    //must checked first since we cannot declare a drag drop source without crashing if sorting
                    if (!taskNeedSort && ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("TASK_ROW", &row, sizeof(int));
                        ImGui::EndDragDropSource();
                    }

                    if (!taskNeedSort && ImGui::BeginDragDropTarget() ) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TASK_ROW")) {
                            if (payload->IsDelivery()) {
                                int fromRow = *(const int*)payload->Data;

                                //Rotate like the name litteary rotate everything in the range so that the middle element becoems the front one
                                if (fromRow != row) {
                                    if (fromRow < row) {
                                        //Since we are dragging down from fromRow we want the element after it to become the first therefore we rotate around it, and since c isint inclusive
                                        //of end bound we go one beyond to  actually reach it and be able to rotate it
                                        std::rotate(taskList.begin() + fromRow,
                                            taskList.begin() + fromRow + 1,
                                            taskList.begin() + row + 1);
                                    }

                                    else {
                                        //Here we are dragging up so we want the fromRow to become the first element so we rotate around it with the bounds being [row, fromrow)
                                        //therefore we do the +1 to atually include fromRow 
                                        std::rotate(taskList.begin() + row,
                                            taskList.begin() + fromRow,
                                            taskList.begin() + fromRow + 1);
                                    }
                                    saved = false;
                                }
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    


                    //Third is its priority (We will convert it to text to show it visually with different priorities being marked clearly)
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text((std::to_string(taskList[row].priority)).c_str());


                    //Fourth is its due date
                    ImGui::TableSetColumnIndex(3);
                    //I:M p converts tm from 24 hour time inputted to easier to read standard time
                    //We add some checks to determine wich color the date should be depending on how soon the task is due
                    //We get local time then we format the due date to be able to compare them using difftime
                    std::time_t currentDate = std::time(nullptr);

                    //A copy to prevent from fuckign with the time in the struct causesd by mktime
                    struct tm temp = taskList[row].dueDate;

                    //tells mktime to figure out DST
                    temp.tm_isdst = -1;

                    std::time_t formatedDueDate = std::mktime(&temp);
                    //Diff time uses two times and determines if a-b
                    double secondsLeft = std::difftime(formatedDueDate, currentDate);

                    ImVec4 dateColor = {};

                    //I'll probably add in settings a way to change which is which but for now we'll keep it like this

                    //if statements to determine due date
                    if (secondsLeft < 0) {
                        dateColor = darkRedColor;
                    }
                    else if (secondsLeft < SECINDAY) {
                        dateColor = redColor;
                    }
                    else if (secondsLeft < 3 * SECINDAY) {
                        dateColor = yellowColor;
                    }
                    else {
                        dateColor = greenColor;
                    }

                    //Copies formated date into a buffer using the fancy time conversion from <time>
                    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %I:%M %p", &taskList[row].dueDate);
                    ImGui::TextColored(dateColor, buffer);


                    //Fifth is its catagory
                    ImGui::TableSetColumnIndex(4);
                    ImGui::PushStyleColor(ImGuiCol_Text, taskList[row].cat.color);
                    ImGui::Text(taskList[row].cat.name);
                    ImGui::PopStyleColor();
                    ImGui::PopID();

                }

                ImGui::EndTable();

            }

            if (taskDone) {
                //Erases outside out of foor loop to prevent out of range glitches
                taskList.erase(taskList.begin() + done_idx);
                taskDone = false;
                done_idx = 0;
            }

            //Handles the actual behaviour inside of the popup

            if (openTaskEdit) {
                ImGui::OpenPopup("##taskEditDelete");
                openTaskEdit = false;
                //Refresh current time and saving it to due date
                t = std::time(nullptr);
                localtime_s(&dueDate, &t);
            }


            if (ImGui::BeginPopup("##taskEditDelete")) {
                //Show name as header
                ImGui::Text(taskList[rightClicked_idxTask].name);
                ImGui::Separator();

                if (ImGui::MenuItem("Edit")) {
                    //We must pass through the initial values to make editing easier instead of having to re-do it fromt scratch
                    taskPopUpOpen = true;
                    isEditingTask = true;
                    //Copy over the variables to the default locations showing them to help prevent repetition of inputs
                    strncpy(taskName, taskList[rightClicked_idxTask].name, sizeof(taskList[rightClicked_idxTask].name) - 1);
                    taskName[sizeof(taskName) - 1] = '\0';
                    priority = taskList[rightClicked_idxTask].priority;
                    isCompleted = taskList[rightClicked_idxTask].completed;
                    dueDate = taskList[rightClicked_idxTask].dueDate;
                    for (int i = 0; i < categories.size(); i++) {
                        if (strcmp(taskList[rightClicked_idxTask].cat.name, categories[i].name) == 0) {
                            selected_idx = i;
                        }
                    }

                }


                //Allows for use to delete item removing it form categories
                if (ImGui::MenuItem("Delete")) {
                    for (int i = 0; i < taskList.size(); i++) {
                        if ((strcmp(taskList[i].name, taskList[rightClicked_idxTask].name)) == 0 && i != rightClicked_idxTask && !hasCopies){
                            hasCopies = true;
                        }
                   }

                    if (!hasCopies) {
                        taskList.erase(taskList.begin() + rightClicked_idxTask);
                        //Needs to resort priorities
                        taskNeedSort = true;

                        //safety to ensure no weird behaviour and no going out of bounds if selected_idx would now be out of range
                        if (selected_idx >= taskList.size()) {
                            selected_idx = 0;
                        }
                    }
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

            if (hasCopies) {
                ImGui::OpenPopup("##multiDeletePopup");
            }

            if (ImGui::BeginPopup("##multiDeletePopup")) {
                if (ImGui::Selectable("Only delete this task")) {
                    taskList.erase(taskList.begin() + rightClicked_idxTask);
                    //safety to ensure no weird behaviour and no going out of bounds if selected_idx would now be out of range
                    if (selected_idx >= taskList.size()) {
                        selected_idx = 0;
                    }
                    taskNeedSort = true;
                    hasCopies = false;
                    ImGui::CloseCurrentPopup();
                }

                if (ImGui::Selectable("Delete all of these tasks")) {
                    char targetName[50];
                    int i = 0;
                    strcpy(targetName, taskList[rightClicked_idxTask].name);
                    //We must do this to prevent segfaults since we are actively re-sizing the array we are deleting from 
                    while(i < taskList.size()){
                            if (strcmp(taskList[i].name, targetName) == 0) {
                                taskList.erase(taskList.begin() + i);
                            }
                            //Only goes up when we save an element otherwise we dont go up since the array just shrunk in size so the new i element is new
                            else {
                                i++;
                            }
                    }
                    taskNeedSort = true;
                    hasCopies = false;
                    ImGui::CloseCurrentPopup();
                }

               
                ImGui::EndPopup();

            }



            //Handles adding tasks to do the to-do list
            ImGui::BeginMenuBar();
            if (ImGui::BeginMenu("New task")) {
                taskPopUpOpen = true; //Since ImGui dosents support opening pop ups inside of menu bars we use a flag instead
                t = std::time(nullptr);
                localtime_s(&dueDate, &t);
                endDate = dueDate;
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();


            if (taskPopUpOpen) {
                ImGui::OpenPopup("Task Management");
                taskModalOpen = true;
                taskPopUpOpen = false; // To prevent it from repeatedly opening the pop up for every fram
            }

            //Draws the popup itsef with all the stuff inside

            //Modals are popups that arent clickable off
            if (ImGui::BeginPopupModal("Task Management", &taskModalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
                //Title With Helpful Information
                ImGui::Text("Enter Task Informatiom Below");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Ctrl Click any slider to type");
                    ImGui::EndTooltip();
                }

                ImGui::Separator();

                //Task name input
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputText("##task info", taskName, IM_COUNTOF(taskName));
                ImGui::SameLine();

                //Set Difficulty
                ImGui::SetNextItemWidth(200.0f);
                ImGui::SliderInt("Difficulty", &priority, 1, 100);

                //Set Due Date
                int hour12 = dueDate.tm_hour % 12;
                if (hour12 == 0) hour12 = 12;
                bool isPM = dueDate.tm_hour >= 12;

                // Build hour items "01" - "12"
                const char* hours[12];
                // 12 strings of 3 chars i.e 01\0 etc...
                char hourLabels[12][3];
                for (int i = 0; i < 12; i++) {
                    //Fancy way o just converting the labels nicely in a for statement to then be used for the combo menu(i.e all the 1->01)
                    snprintf(hourLabels[i], sizeof(hourLabels[i]), "%02d", i + 1);
                    hours[i] = hourLabels[i];
                }

                // Build minute items "00", "05", "10" ... or every minute
                const char* mins[60];
                // 60 strings of 3 chars i.e 01\0 etc...
                char minLabels[60][3];
                for (int i = 0; i < 60; i++) {
                    snprintf(minLabels[i], sizeof(minLabels[i]), "%02d", i);
                    mins[i] = minLabels[i];
                }

                int hourIndex = hour12 - 1;
                int minIndex = dueDate.tm_min;

                ImGui::SetNextItemWidth(60);
                if (ImGui::Combo("##hour", &hourIndex, hours, 12))
                    dueDate.tm_hour = (hourIndex + 1) % 12 + (isPM ? 12 : 0);

                ImGui::SameLine();
                ImGui::TextUnformatted(":");
                ImGui::SameLine();

                ImGui::SetNextItemWidth(60);
                if (ImGui::Combo("##min", &minIndex, mins, 60))
                    dueDate.tm_min = minIndex;

                ImGui::SameLine();
                if (ImGui::Button(isPM ? "PM" : "AM"))
                    dueDate.tm_hour = (dueDate.tm_hour + 12) % 24;


                ///Repeat Task widget

                //Adds ability for a task to repeat on a certain interval 
                ImGui::SameLine();
                const char* repeatPeriods[] = {"None", "Days", "Weeks", "Months" };
                char previewLabel[64] = {};
                //Handles the different behavirous of it does actually repeat vs if it dosent
                if(repeat_idx == 0){
                    snprintf(previewLabel, sizeof(previewLabel), "Does not repeat");
                }
                else {
                    snprintf(previewLabel, sizeof(previewLabel), "Every %d, %s", repeatInterval, repeatPeriods[repeat_idx]);
                }

               if (ImGui::BeginCombo("Repeats", previewLabel, ImGuiSelectableFlags_DontClosePopups)) {
                    for (int i = 0; i < 4; i++) {
                        if (ImGui::Selectable(repeatPeriods[i], repeat_idx == i)) {
                            repeat_idx = i;
                        }

                    }
                    ImGui::EndCombo();
                }

               if (repeat_idx != 0) {
                   if (!repeatForever) {
                       ImGui::Text("End Date");
                       ImGui::SameLine();
                       ImGui::DatePicker("##EndDate", endDate);
                       ImGui::SameLine();
                   }
                   ImGui::SliderInt("##interval", &repeatInterval, 1, 31);
                   ImGui::SameLine();
                   ImGui::Text(repeatPeriods[repeat_idx]);
                   ImGui::Checkbox("Repeat Forever", &repeatForever);
               }


                
                ImGui::Text("Due Date");
                ImGui::SameLine();
                ImGui::DatePicker("##datePicker", dueDate);
                ImGui::SameLine();

                //Catergory Input
                ImGui::SetNextItemWidth(200.0f);
                //Create a combo with selectables representing every Catergory
                //The push and pop style color make sure its displayed in the appropriate color
                ImGui::PushStyleColor(ImGuiCol_Text, categories[selected_idx].color);
                bool comboOpen = (ImGui::BeginCombo("##Category", categories[selected_idx].name));
                ImGui::PopStyleColor();

                if (comboOpen) {
                    if (catNeedSort) {
                        //Keep track of what user selected before sorting positon
                        category selectedCat = categories[selected_idx];

                        std::stable_sort(categories.begin(), categories.end(),
                            [](const category& a, const category& b) {
                                return a.priority > b.priority; // Since we want a to be infront of b to be true if its greater then b
                            });

                        //Now after sorting the order of list changed so we need to update the selection of the user to still be accurate

                        for (int i = 0; i < categories.size(); i++) {
                            if (strcmp(selectedCat.name,categories[i].name) == 0) {
                                selected_idx = i;
                                break;
                            }
                        }


                        //Makr that the sorting has been done and it isint nessesary to resort
                        catNeedSort = false;
                    }

                    for (int i = 0; i < categories.size(); i++) {
                        //If the selectable itself is clicked adds to its priority which will be used to determine its position in the list as well as set it as the index
                        //Stores which one was prevouisly clicked setting it true and making it clear
                        const bool selected = (selected_idx == i);
                        ImGui::PushStyleColor(ImGuiCol_Text, categories[i].color);
                        //Whenever somethign i seleted we save its index and we also increment its priority so more oftenly used categories show up higher
                        if (ImGui::Selectable(categories[i].name, selected)) {
                            categories[i].priority += 1;
                            selected_idx = i;
                            catNeedSort = true;
                        }
                        ImGui::PopStyleColor();

                        //Ensure that if user hovers and right clicks a category we open up the category edit/delte popup menu
                        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            rightClicked_idxCat = i;
                            ImGui::OpenPopup("##CategoryEditDelete");
                        }

                        //Makes it focus for readability
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    if (ImGui::BeginPopup("##CategoryEditDelete")) {
                        //Show name as header
                        ImGui::Text(categories[rightClicked_idxCat].name);
                        ImGui::Separator();

                        if (ImGui::MenuItem("Edit")) {
                            //We must pass through the initial values to make editing easier instead of having to re-do it fromt scratch
                            openCategoryPopup = true;
                            isEditingCat = true;
                            strncpy(categoryName, categories[rightClicked_idxCat].name, sizeof(categories[rightClicked_idxCat].name) - 1);
                            categoryName[sizeof(categoryName)-1] = '\0';
                            catPriority = categories[rightClicked_idxCat].priority;
                            color = categories[rightClicked_idxCat].color;
                        }

                      
                        //Allows for use to delete item removing it form categories
                        if (ImGui::MenuItem("Delete")) {
                            if (categories.size() > 1) {
                                //Re-assign all task with this cateogores to most common one
                                int fallBack_idx = (rightClicked_idxCat == 0) ? 1 : 0; //Safety so if they are trying to delete the most popular cat we fall back to the second most popular one

                                for (int i = 0; i < taskList.size(); i++) {
                                    if (strcmp(taskList[i].cat.name, categories[rightClicked_idxCat].name) == 0) {
                                        taskList[i].cat = categories[fallBack_idx];
                                    }
                                }

                                categories.erase(categories.begin() + rightClicked_idxCat);
                                //Needs to resort priorities
                                catNeedSort = true;
                                //safety to ensure no weird behaviour and no going out of bounds if selected_idx would now be out of range
                                if (selected_idx >= categories.size()) {
                                    selected_idx = 0;
                                }

                                ImGui::CloseCurrentPopup();
                            }
                            else {
                                //Set off flag to show cannot delete popup outside since ImGui cannot do nested popups well
                                showCantDelete = true;
                            }
                        }
                       

                        ImGui::EndPopup();
                    }


                    //Handles the popup to show that you must at least one category
                    if (showCantDelete) {
                        ImGui::OpenPopup("##cantDelete");
                    }

                    if (ImGui::BeginPopup("##cantDelete")) {
                        ImGui::Text("Must have at least one category");
                        ImGui::Spacing();


                        //We want to centre the ok button so we do some math
                        float windowWidth = ImGui::GetWindowWidth();
                        float buttonWidth = 120.0f;
                        ImGui::SetCursorPosX( (windowWidth - buttonWidth) / 2.0f);

                        if (ImGui::Button("Ok", ImVec2(buttonWidth, 0.0f))){
                            showCantDelete = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }


                    //Gives option of adding catagories
                    ImGui::Separator();

                    if (ImGui::Selectable("+ Add Catergory")) {
                        openCategoryPopup = true;
                    }

                    ImGui::EndCombo();
                }

                if (openCategoryPopup) {
                    ImGui::OpenPopup("Category Information");
                    categoryModalOpen = true;
                    openCategoryPopup = false;
                }

                //Create a modal where the user can input information about Catergory in appropriate structure
                if (ImGui::BeginPopupModal("Category Information", &categoryModalOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::InputText("##Category name", categoryName, IM_COUNTOF(categoryName));
                    ImGui::PopStyleColor();
                    ImGui::ColorEdit3("Color##", (float*)&color);

                    if (ImGui::Button("+") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                        if (isEditingCat) {
                            strncpy(categories[rightClicked_idxCat].name, categoryName, sizeof(categoryName) - 1);
                            categories[rightClicked_idxCat].priority = catPriority;
                            categories[rightClicked_idxCat].color = color;
                            isEditingCat = false;
                        }
                        else
                        {
                            category c = { "", catPriority, color };
                            strncpy(c.name, categoryName, sizeof(categoryName) - 1);
                            c.name[sizeof(c.name) - 1] = '\0';
                            categories.push_back(c);

                        }
                        ImGui::CloseCurrentPopup();

                        //Resets everything to initial conditions
                        strncpy(categoryName, "Enter Catergory", sizeof(categoryName) - 1);
                        catPriority = 0;
                        color = defaultColor;
                        openCategoryPopup = false;
                        saved = false;
                    }

                    ImGui::EndPopup();
                }


                //Once all the information is gathered we insert it into the task list then we close the popup(added extra check to make sure not closing it at wrong time)
                if (ImGui::Button("Done") || ImGui::IsKeyPressed(ImGuiKey_Enter) && !openCategoryPopup) {
                    //Has to be done like this since strings arent friendly and require to be careful with memory forcing the use of strcpy
                    if (isEditingTask) {
                        strncpy(taskList[rightClicked_idxTask].name, taskName, sizeof(taskName) - 1);
                        taskList[rightClicked_idxTask].name[sizeof(taskList[rightClicked_idxTask].name) - 1] = '\0';
                        taskList[rightClicked_idxTask].priority = priority;
                        taskList[rightClicked_idxTask].completed = false;
                        taskList[rightClicked_idxTask].dueDate = dueDate;
                        taskList[rightClicked_idxTask].cat = categories[selected_idx];
                        isEditingTask = false;

                    }
                    else {
                        task t = { "", priority, false, dueDate, categories[selected_idx] };
                        strncpy(t.name, taskName, sizeof(t.name) - 1);
                        t.name[sizeof(t.name) - 1] = '\0';
                        taskList.push_back(t);

                        //Handles cases in creation were a task is repeated througg first calling a function to find # of times a repeat occurs before end date
                        //
                        if (repeat_idx != 0) {
                            int occurences = 1;
                            if (repeatForever) {
                                occurences = 200;
                            }
                            else {
                                occurences = getOccurrences(dueDate, endDate, repeatInterval, repeat_idx);
                                printf("%d", occurences);
                            }
                            for (int i = 1; i <= occurences; i++) {
                                struct tm repeatedDate = dueDate; //protectin due date from changes caused by functions
                                //We need to times by i sincewe are making a fresh copy of due date for reach meeaning that they will be incorrect otherwise
                                switch (repeat_idx) {
                                    case 1: repeatedDate.tm_mday += repeatInterval*i; break;
                                    case 2: repeatedDate.tm_mday += repeatInterval * 7*i; break;
                                    case 3: repeatedDate.tm_mon += repeatInterval*i; break;
                                }
                                mktime(&repeatedDate); //Normalizes overflow
                                task rt = {"", priority, false, repeatedDate, categories[selected_idx]};
                                strncpy(rt.name, taskName, sizeof(rt.name) - 1);
                                rt.name[sizeof(rt.name) - 1] = '\0';
                                taskList.push_back(rt);
                            }


                        }


                    }

                    strncpy(taskName, "Enter Task", sizeof(taskName) - 1);
                    priority = 50;
                    saved = false;
                    repeat_idx = 0;
                    repeatInterval = 1;
                    repeatForever = true;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }


            ImGui::End();
        }

        /////////////////////////////////////////////////// TASKS DONE WINDOW ////////////////////////////////////////////////////////////////////////////

        if (drawTasksDone) {
            ImGui::Begin("Tasks Done", &drawTasksDone, ImGuiWindowFlags_MenuBar);//Flag needed for new task to be in the menu bar

            //We seperate completion of task into seperate buckets which track which week each task was completed showing off work done per week
            //Can think of this as a 2d array but with the 2 things beign anything we want like vectors and ints(first being key and the second being where it leads)

            if (weekBucketsDirty) {
                weekBuckets.clear();
                completedWeekBuckets.clear();

                time_t now = time(nullptr);
                struct tm nowTm = {};
                localtime_s(&nowTm, &now); //Converts nowTm to be the local time

                //Zero out unimportant tm components to make the second math more accurate
                nowTm.tm_hour = 0; nowTm.tm_min = 0; nowTm.tm_sec = 0;

                //tm_wday is which day of the week with sunday being 0 while tm_mday is the day in the month 1-31 so we can sub to find which sunday in the month we are
                nowTm.tm_mday -= nowTm.tm_wday;
                //Enalbes day lights saving
                nowTm.tm_isdst = -1;

                //converts to usable format for math
                time_t weekStart = mktime(&nowTm);


                //We want to first purge all the old stuff that is older then 3 weeks since its no longer useful

                int i = 0;

                while (i < completedTaskList.size()) {
                    struct tm temp = completedTaskList[i].dueDate;
                    temp.tm_isdst = -1;
                    time_t taskTime = mktime(&temp);

                    int weeksAgo = (int)(difftime(taskTime, weekStart) / (7 * SECINDAY));

                    //Delete the old ones when they are more then three weeks ago
                    if (weeksAgo < -3) {
                        completedTaskList.erase(completedTaskList.begin() + i);
                        saved = false;
                    }
                    else {
                        //Otherwise increment spot in vector
                        i++;
                    }
                }


                //auto& basically tells you to for loop and that t is the entity is  so this is just a quick way to loop through the entire vector
                for (auto& t : completedTaskList) {
                    struct tm temp = t.dueDate;
                    temp.tm_isdst = -1;
                    //to prevent desctruction of due date
                    time_t taskTime = mktime(&temp);

                    int weeksAgo = (int)(difftime(taskTime, weekStart) / (SECINDAY * 7));// Basically figure out how many weeks off this is - being before this week start
                    //and + being after this week start a whole number means its a full week diff otherwise its same week

                    completedWeekBuckets[weeksAgo].push_back(t);
                }

                //Handles sorting for the uncompleted one so we can atcually dispaly progress
                for (auto& t : taskList) {
                    struct tm temp = t.dueDate;
                    temp.tm_isdst = -1;
                    //to prevent desctruction of due date
                    time_t taskTime = mktime(&temp);

                    int weeksAgo = (int)(difftime(taskTime, weekStart) / (SECINDAY * 7));// Basically figure out how many weeks off this is - being before this week start
                    //and + being after this week start a whole number means its a full week diff otherwise its same week

                    weekBuckets[weeksAgo].push_back(t);
                }

                //We now want to check through and delete anything older then 3 weeks since they are no longer important
                weekBucketsDirty = false;
            }


            float progress;
            char overlay[52];
            char barString[52];

            //Since we dont want to access empty maps/vectors we use this as a check to just make it 0 if it fails
            int completedCount = completedWeekBuckets.count(selectedWeek) ? (int)completedWeekBuckets[selectedWeek].size() : 0;
            int pendingCount = weekBuckets.count(selectedWeek) ? (int)weekBuckets[selectedWeek].size() : 0;
            int total = completedCount + pendingCount;


            const char* weekWord = (abs(selectedWeek) == 1) ? "Week" : "Weeks"; //pluralizes when nesseasry

            if (selectedWeek == 0) {
                snprintf(overlay, sizeof(overlay), "This Week");
            }
            else if (selectedWeek < 0) {
                snprintf(overlay, sizeof(overlay), "%d %s Ago", abs(selectedWeek), weekWord);
            }
            else {
                snprintf(overlay, sizeof(overlay), "In %d %s", selectedWeek, weekWord);
            }

            snprintf(barString, sizeof(barString), "%d : %d", completedCount, total);

            if (total != 0) {
                progress = (float) completedCount / (float)(total);
            }
            else {
                progress = 0;
            }

            //Directional arrows to allow to go through the week with clamps preventing you from going more then 3 weeks in both direction
            //This is important for prev week as im going to add auto-clearing of completed task older then a week
            if (selectedWeek > -3) {
                if (ImGui::ArrowButton("##prevWeek", ImGuiDir_Left)) {
                    selectedWeek--;
                }
            }

            ImGui::SameLine();
            ImGui::Text(overlay);
            ImGui::SameLine();

           if (ImGui::ArrowButton("##nextWeek", ImGuiDir_Right)) {
                selectedWeek++;
           }
           

            ImGui::Separator();

            //-FLT_MIN tells the progress bar to basically fill out the entire row
            //We add fancy statements so that bar changes color depending on progress
            ImVec4 barColor;

            //Color gradient
            if (progress < 0.33f) barColor = redColor; // red
            else if (progress < 0.66f) barColor = yellowColor; // yellow
            else                        barColor = greenColor; // green

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
            ImGui::ProgressBar(progress, ImVec2(-FLT_MIN,0), barString);
            ImGui::PopStyleColor();

            //Table of completed task begin basically the same as the last one minus dragging and editing since those arent important
            if (ImGui::BeginTable("Tasks Completed", 5, tableFlags)) {

                //Headers for table
                //We setup up in column style since we want to enable sorting in the flags and are unable to do it this way otherwise
                ImGui::TableSetupColumn("Done##2", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthFixed, 60.f);
                ImGui::TableSetupColumn("Name##2", ImGuiTableColumnFlags_None);
                ImGui::TableSetupColumn("Difficulty##2", ImGuiTableColumnFlags_PreferSortDescending);
                ImGui::TableSetupColumn("Due Dat##2", ImGuiTableColumnFlags_PreferSortAscending);
                ImGui::TableSetupColumn("Category##2", ImGuiTableColumnFlags_None);
                ImGui::TableHeadersRow(); // Renders the header row with sort arrows automatically

                //Sorting logic for table that is called appropriatly

                if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs()) {
                    //ImGui has a crap ton of logic for spec sorting already so were gonna mainly use that for sorting where we can
                    completedTaskNeedSort = sortSpecs->SpecsCount > 0;

                    if (sortSpecs->SpecsDirty) {
                        //Basically marks when something caused table to become unsorted(which is true on first frame)


                        //You can shift click to sort multiple columns at once so we need to loop through them to account for that(Its what specsCount Tracks)
                        for (int n = 0; n < sortSpecs->SpecsCount;n++) {
                            //Create a nice variable to accces a single spec
                            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[n];

                            //Check if the arrow is ascending or descending and save it as a bool
                            bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

                            //Now depending on which column it is we need to different things so we do a simple switch case

                            switch (spec.ColumnIndex) {
                            case 1://Name
                                //Stable sort takes (begin, end, comparison) whith compariso deciding how we decide the order
                                std::stable_sort(completedTaskList.begin(), completedTaskList.end(),
                                    //Capture ascneding so its usable in the stable sort comparison
                                    [ascending](const task& a, const task& b) {
                                        //Runs comparison function from string which returns greater then 0 if a first char and so on is greater then b char
                                        int cmp = strcmp(a.name, b.name);
                                        //if ascending is true we return the result of comparison < 0 i.e a is less then b(i.e true for ascending)
                                        //else it returns false meaning while a is less then b we actaully want higher cmp first
                                        return ascending ? cmp < 0 : cmp > 0;
                                    }
                                    //Stable sort itself handles the re-ordering through internal calls and pass by references for us
                                );
                                //End of switch case
                                break;


                            case 2: //Priority
                                std::stable_sort(completedTaskList.begin(), completedTaskList.end(), [ascending](const task& a, const task& b) {
                                    //We basically just return that true if for ascneding a is greater then b and vice versa if descending
                                    return ascending ? a.priority > b.priority : a.priority < b.priority;
                                    }
                                );

                                break;

                            case 3:  //Due Date
                                std::stable_sort(completedTaskList.begin(), completedTaskList.end(), [ascending](const task& a, const task& b) {
                                    //Converts tm to time_t which is just a single large integer making comparison easy
                                    //mktime just handles the breaking down of tm to time_t as a function
                                    struct tm tempA = a.dueDate;
                                    struct tm tempB = b.dueDate;
                                    tempA.tm_isdst = -1;
                                    tempB.tm_isdst = -1;
                                    time_t ta = mktime(&tempA);
                                    time_t tb = mktime(&tempB);
                                    return ascending ? ta > tb : ta < tb;
                                    });
                                break;

                            case 4: //Category
                                std::stable_sort(completedTaskList.begin(), completedTaskList.end(),
                                    [ascending](const task& a, const task& b) {
                                        int cmp = strcmp(a.cat.name, b.cat.name);
                                        return ascending ? cmp < 0 : cmp > 0;
                                    }
                                    //Stable sort itself handles the re-ordering through internal calls and pass by references for us
                                );
                                break;
                            }


                        }
                        //Marks that we finished sorting so no sorting logic needs to be run
                        sortSpecs->SpecsDirty = false;
                        weekBucketsDirty = true; //Telling to redraw the buckets so its accurate to the now sorted list!
                    }

                }

                if (completedWeekBuckets.count(selectedWeek)) {
                    auto& currentWeek = completedWeekBuckets[selectedWeek];
                    //Checks if its properly sorted which will be used later to prevent dragging behaviour from occuring
                    for (int row = 0; row < currentWeek.size(); row++) {
                        //Creates a row
                        ImGui::TableNextRow();
                        ImGui::PushID(row);

                        //First element in index is a checkbox to complete task
                        ImGui::TableSetColumnIndex(0);
                        //ImGui::Checkbox("##compelte", &taskList[row].completed);
                        if (ImGui::Checkbox("##complete2", &currentWeek[row].completed)) {
                            if (!currentWeek[row].completed) {
                                //We loop through all the completed task list to find the appropriate one
                                for (int i = 0; i < completedTaskList.size(); i++) {
                                    if (strcmp(completedTaskList[i].name, currentWeek[row].name) == 0) {
                                        //Since we can repeated task we make sure these are on the same day
                                        if (completedTaskList[i].dueDate.tm_mday == currentWeek[row].dueDate.tm_mday) {
                                            undone_idx = i;
                                            break;
                                        }
                                    }
                                }


                                taskUndone = true;
                                taskList.push_back(currentWeek[row]);
                                weekBucketsDirty = true;
                            }
                        }

                        //Second is its name
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Selectable(currentWeek[row].name, false, ImGuiSelectableFlags_SpanAllColumns);


                        //Third is its priority (We will convert it to text to show it visually with different priorities being marked clearly)
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text((std::to_string(currentWeek[row].priority)).c_str());


                        //Fourth is its due date
                        ImGui::TableSetColumnIndex(3);
                        //I:M p converts tm from 24 hour time inputted to easier to read standard time
                        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %I:%M %p", &currentWeek[row].dueDate);
                        ImGui::Text(buffer);


                        //Fifth is its catagory
                        ImGui::TableSetColumnIndex(4);
                        ImGui::PushStyleColor(ImGuiCol_Text, currentWeek[row].cat.color);
                        ImGui::Text(currentWeek[row].cat.name);
                        ImGui::PopStyleColor();

                        ImGui::PopID();

                    }

                }

                ImGui::EndTable();

            }

            //Handles re-checking to revive task
            if (taskUndone) {
                completedTaskList.erase(completedTaskList.begin() + undone_idx);
                taskUndone = false;
                completedTaskNeedSort = true;
            }

            ImGui::End();
        }

    ///////////////////////////////////////////// RENDERING //////////////////////////////////////////////////////////////////////////////////

        ImGui::Render();

        FrameContext* frameCtx = WaitForNextFrameContext();
        UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
        frameCtx->CommandAllocator->Reset();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = g_mainRenderTargetResource[backBufferIdx];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
        g_pd3dCommandList->ResourceBarrier(1, &barrier);

        // Render Dear ImGui graphics
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
        g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
        g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        g_pd3dCommandList->ResourceBarrier(1, &barrier);
        g_pd3dCommandList->Close();

        g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);
        g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
        frameCtx->FenceValue = g_fenceLastSignaledValue;

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, g_SwapChainTearingSupport ? DXGI_PRESENT_ALLOW_TEARING : 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
        g_frameIndex++;
    }

    WaitForPendingOperations();
    //We do a quick save here to auto-save
    saveFile(taskList, categories, completedTaskList, getSavePath());

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}


/// //////////////////////////////////////////////////////////////////////////////// HELPER FUNCTIONS///////////////////////////////////////////////////////

//This function is reponsible for opening a file thats passed through the file finder and finding and copying the information it has
void openFile(std::vector<task>& list, std::vector<category>& categories, std::vector <task> &completedList,  const char* filename) {
    if (strlen(filename) == 0) return;

    FILE* f = fopen(filename, "r");
    if (f == NULL) return;

    char line[256];
    list.clear();
    completedList.clear();
    categories.clear();

    while (fgets(line, sizeof(line), f)) {

        //Handles category
        if (strncmp(line, "CAT:", 4) == 0) {
            category c;
            if (sscanf(line + 4, "%[^|]|%d|%f|%f|%f|%f",
                c.name,
                &c.priority,
                &c.color.x,
                &c.color.y,
                &c.color.z,
                &c.color.w
            ) == 6) {
                categories.push_back(c);
            }
        }

        //Handles for uncompleted task
        else if(strncmp(line, "UNDONE:",7)==0){
            task t;
            int comp;
            if (sscanf(line+7, "%[^|]|%d|%d|%d|%d|%d|%d|%d|%[^|]|%d|%f|%f|%f|%f",
                t.name,
                &t.priority,
                &comp,
                &t.dueDate.tm_year,
                &t.dueDate.tm_mon,
                &t.dueDate.tm_mday,
                &t.dueDate.tm_hour,
                &t.dueDate.tm_min,
                t.cat.name,      //category name
                &t.cat.priority, //category priority
                &t.cat.color.x, //r
                &t.cat.color.y, //g
                &t.cat.color.z, //b
                &t.cat.color.w //a
            ) == 14) {
                t.completed = (comp == 1);
                list.push_back(t);
            }
        }
        //Handles it for completed task
        else if (strncmp(line, "DONE:", 5) == 0) {
            task t;
            int comp;
            //the [^|] just shows that it keep reading after the space
            if (sscanf(line + 5, "%[^|]|%d|%d|%d|%d|%d|%d|%d|%[^|]|%d|%f|%f|%f|%f",
                t.name,
                &t.priority,
                &comp,
                &t.dueDate.tm_year,
                &t.dueDate.tm_mon,
                &t.dueDate.tm_mday,
                &t.dueDate.tm_hour,
                &t.dueDate.tm_min,
                t.cat.name,      //category name
                &t.cat.priority, //category priority
                &t.cat.color.x, //r
                &t.cat.color.y, //g
                &t.cat.color.z, //b
                &t.cat.color.w //a
            ) == 14) {
                t.completed = (comp == 1);
                completedList.push_back(t);
            }
        }
    }
    fclose(f);


}



void newfile(std::vector<task>& list, std::vector<category>& categories, std::vector<task> &completedList, const char* filename) {
    if (strlen(filename) == 0) return;
    FILE* f = fopen(filename, "w");
    if (!f) return;

    list.clear();
    categories.clear();
    completedList.clear();

    ImVec4 defaultColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    categories.push_back({ "General", 0, defaultColor });
    fclose(f);

    saveFile(list, categories, completedList, filename);
}

void saveFile(std::vector<task>& taskList, std::vector<category> &categories, std::vector<task>& completedList, const char* filename) {
    if (strlen(filename) == 0) return;

    FILE* f = fopen(filename, "w");
    if (!f) return;

    for (int i = 0; i < categories.size(); i++) {
        //Originally used saved files with the , seperator but swapped it to | which is unlikely to be used
        fprintf(f, "CAT:%s|%d|%f|%f|%f|%f\n",
            categories[i].name,
            categories[i].priority,
            categories[i].color.x,
            categories[i].color.y,
            categories[i].color.z,
            categories[i].color.w
            );

    }


    for (int i = 0; i < taskList.size(); i++) {
        fprintf(f, "UNDONE:%s|%d|%d|%d|%d|%d|%d|%d|%s|%d|%f|%f|%f|%f\n",
            taskList[i].name,
            taskList[i].priority,
            taskList[i].completed,
            taskList[i].dueDate.tm_year,
            taskList[i].dueDate.tm_mon,
            taskList[i].dueDate.tm_mday,
            taskList[i].dueDate.tm_hour,
            taskList[i].dueDate.tm_min,
            taskList[i].cat.name,       //Category name
            taskList[i].cat.priority,   //Priority
            taskList[i].cat.color.x, //r
            taskList[i].cat.color.y, //g
            taskList[i].cat.color.z, //b
            taskList[i].cat.color.w //a
            );
    }

    for (int i = 0; i < completedTaskList.size(); i++) {
        fprintf(f, "DONE:%s|%d|%d|%d|%d|%d|%d|%d|%s|%d|%f|%f|%f|%f\n",
            completedTaskList[i].name,
            completedTaskList[i].priority,
            completedTaskList[i].completed,
            completedTaskList[i].dueDate.tm_year,
            completedTaskList[i].dueDate.tm_mon,
            completedTaskList[i].dueDate.tm_mday,
            completedTaskList[i].dueDate.tm_hour,
            completedTaskList[i].dueDate.tm_min,
            completedTaskList[i].cat.name,       //Category name
            completedTaskList[i].cat.priority,   //Priority
            completedTaskList[i].cat.color.x, //r
            completedTaskList[i].cat.color.y, //g
            completedTaskList[i].cat.color.z, //b
            completedTaskList[i].cat.color.w //a
        );

    }
    fclose(f);
}


std::string GetPathFromUser(bool saveMode) {
    OPENFILENAMEA ofn;       // The "Form"
    char szFile[260] = { 0 }; // The "Buffer" for the result

    ZeroMemory(&ofn, sizeof(ofn)); // memset to 0
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    // This is the part that was confusing:
    BOOL success;
    if (saveMode == true) {
        // Use the "Save" version of the window
        success = GetSaveFileNameA(&ofn);
    }
    else {
        // Use the "Open" version of the window
        success = GetOpenFileNameA(&ofn);
    }

    if (success) {
        std::string path(szFile);
        if (path.length() < 4 || path.substr(path.length() - 4) != ".txt") {
            path += ".txt";
        }
        return path;

    }
    else {
        return ""; // Return empty if they closed the window
    }
}

const char* getSavePath() {
    return (strlen(lastUsedPath) > 0) ? lastUsedPath : autosavePath;
}

void saveConfig(const char* lastPath) {
    char configPath[260] = "";
    //This stuff is weird but basically it goes to the exe and gives you its path wich we then cocatenatae with the appropriate extra info
    GetModuleFileNameA(nullptr, configPath, sizeof(configPath));
    char* lastSlash = strrchr(configPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat(configPath, "..\\saves\\config.txt");

    FILE* f = fopen(configPath, "w");
    if (!f) return;
    fprintf(f, "%s\n", lastPath);
    fclose(f);
}


void loadConfig(char* lastPath, int size) {
    char configPath[260];

    GetModuleFileNameA(nullptr, configPath, sizeof(configPath));
    char* lastSlash = strrchr(configPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat(configPath, "..\\saves\\config.txt");

    FILE* f = fopen(configPath, "r");
    if (!f) return;

    //Cant be constant unlike other one sicne it needs to change it
    fgets(lastPath, size, f);

    //We need to strip newline char fgets leaves
    char* newLine = strchr(lastPath, '\n'); //Finds new line char left
    if (newLine) *newLine = '\0'; //If there is one we replace it with a null terminator
    fclose(f);
}



//Counts how many times the interval is in till the end date and returns it as an int for easy math
int getOccurrences(tm dueDate, tm endDate, int repeatInt, int idx) {
    int occurences = 0;

    struct tm tempDue = dueDate;
    struct tm tempEnd = endDate;


    tempDue.tm_isdst = -1;
    tempEnd.tm_isdst = -1;

    std::time_t dueTime = std::mktime(&tempDue);
    std::time_t endTime = std::mktime(&tempEnd);

    //Finds time1 - time2 i.e difference between the two in seconds
    double secondDiff = difftime(endTime, dueTime);


    switch (idx) {
    case 1: return (int)(secondDiff / SECINDAY) / repeatInt; break;
    case 2: return (int)(secondDiff / (SECINDAY*7)) / repeatInt; break;
    case 3: return (int)(secondDiff / (SECINDAY*30)) / repeatInt; break;
    default: return 0;
    }

}




//////////////////////////////////////////////////////// IMGUI BUILT IN HELPERS(DONT TOUCH)///////////////////////////////////////////////////////////////////////////
bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could handle fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = APP_NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    // [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    // Create device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

    // [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

        // Disable breaking on this warning because of a suspected bug in the D3D12 SDK layer, see #9084 for details.
        const int D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ = 1424; // not in all copies of d3d12sdklayers.h
        D3D12_MESSAGE_ID disabledMessages[] = { (D3D12_MESSAGE_ID)D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = 1;
        filter.DenyList.pIDList = disabledMessages;
        pInfoQueue->AddStorageFilterEntries(&filter);

        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;

        SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        {
            g_mainRenderTargetDescriptor[i] = rtvHandle;
            rtvHandle.ptr += rtvDescriptorSize;
        }
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
        g_pd3dSrvDescHeapAlloc.Create(g_pd3dDevice, g_pd3dSrvDescHeap);
    }

    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
        return false;

    {
        IDXGIFactory5* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
            return false;

        BOOL allow_tearing = FALSE;
        dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
        g_SwapChainTearingSupport = (allow_tearing == TRUE);
        if (g_SwapChainTearingSupport)
            sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
            return false;
        if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
            return false;
        if (g_SwapChainTearingSupport)
            dxgiFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

        swapChain1->Release();
        dxgiFactory->Release();
        g_pSwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
        g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
    if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
    if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
    if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
    if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

void CreateRenderTarget()
{
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
        g_mainRenderTargetResource[i] = pBackBuffer;
    }
}

void CleanupRenderTarget()
{
    WaitForPendingOperations();

    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
}

void WaitForPendingOperations()
{
    g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);

    g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
    ::WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameContext()
{
    FrameContext* frame_context = &g_frameContext[g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT];
    if (g_fence->GetCompletedValue() < frame_context->FenceValue)
    {
        g_fence->SetEventOnCompletion(frame_context->FenceValue, g_fenceEvent);
        HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, g_fenceEvent };
        ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
    }
    else
        ::WaitForSingleObject(g_hSwapChainWaitableObject, INFINITE);

    return frame_context;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            g_pSwapChain->GetDesc1(&desc);
            HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
            IM_ASSERT(SUCCEEDED(result) && "Failed to resize swapchain.");
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        saveFile(taskList, categories, completedTaskList, getSavePath());
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
