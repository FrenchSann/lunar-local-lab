#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <locale>

namespace fs = std::filesystem;
namespace {
HWND window, pidBox, pathBox, output, inputs[4], checks[3];
HFONT font;
std::thread worker;
bool busy = false;
constexpr UINT finished = WM_APP + 1;
std::vector<HWND> buttons;
std::wstring result;
std::wstring value(HWND control) {
    int size = GetWindowTextLengthW(control);
    std::wstring text(size + 1, L'\0');
    GetWindowTextW(control, text.data(), size + 1); text.resize(size); return text;
}
struct Settings {
    double reach, threshold; int period, timeout;
};
double numeric(const std::wstring& text) {
    std::wistringstream stream(text); stream.imbue(std::locale::classic());
    double number; if (!(stream >> number) || !std::isfinite(number)) throw std::runtime_error("number");
    stream >> std::ws; if (!stream.eof()) throw std::runtime_error("suffix"); return number;
}
Settings validate(const std::wstring& reach, const std::wstring& period, const std::wstring& threshold, const std::wstring& timeout) {
    double r=numeric(reach), p=numeric(period), t=numeric(threshold), d=numeric(timeout);
    if(r<=0||r>6||p<20||p>1000||p!=std::floor(p)||t<0||t>1||d<500||d>10000||d!=std::floor(d)) throw std::runtime_error("range");
    return {r,t,static_cast<int>(p),static_cast<int>(d)};
}
std::wstring widen(const std::string& text) {
    int n=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring out(n,L' '); MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n); return out;
}
void status(const std::wstring& text) { SetWindowTextW(output,text.c_str()); }
HWND control(const wchar_t* kind,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND item=CreateWindowExW(kind==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return item;
}
void load() {
    std::ifstream file{fs::path(value(pathBox))};
    if(!file){status(L"Fichier absent. Ajustez les valeurs puis enregistrez.");return;}
    std::wstring proposed[4]={L"3.0",L"100",L"0.15",L"1500"}; bool enabled[3]={true,true,true};
    const std::string keys[]={"reach","repeatMillis","armorThreshold","armorTimeoutMillis"};
    const std::string flags[]={"repeatEnabled","armorEnabled","selectionEnabled"};
    std::string line;
    while(std::getline(file,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto eq=line.find('=');if(eq==std::string::npos)continue;
        auto key=line.substr(0,eq), v=line.substr(eq+1);
        for(int i=0;i<4;++i)if(key==keys[i])proposed[i]=widen(v);
        for(int i=0;i<3;++i)if(key==flags[i]){if(v!="true"&&v!="false")throw std::runtime_error("boolean");enabled[i]=v=="true";}
    }
    validate(proposed[0],proposed[1],proposed[2],proposed[3]);
    for(int i=0;i<4;++i)SetWindowTextW(inputs[i],proposed[i].c_str());
    for(int i=0;i<3;++i)SendMessageW(checks[i],BM_SETCHECK,enabled[i]?BST_CHECKED:BST_UNCHECKED,0);
    status(L"Configuration chargee. Les valeurs affichees ne prouvent pas leur application dans le jeu.");
}
void save() {
    auto s=validate(value(inputs[0]),value(inputs[1]),value(inputs[2]),value(inputs[3]));
    fs::path path(value(pathBox));
    if(path.filename()!=L"lunar-local-lab.properties"||!fs::is_directory(path.parent_path()))throw std::runtime_error("path");
    auto temp=path;temp+=L".gui.tmp";
    // A unique sibling avoids clobbering another writer's temporary file.
    temp+=std::to_wstring(GetCurrentProcessId());
    std::ostringstream data; data.imbue(std::locale::classic());
    data<<"# Lunar Local Lab - GUI configuration\nreach="<<s.reach<<"\nrepeatMillis="<<s.period<<"\narmorThreshold="<<s.threshold<<"\narmorTimeoutMillis="<<s.timeout<<'\n';
    const char* flags[]={"repeatEnabled","armorEnabled","selectionEnabled"};
    for(int i=0;i<3;++i)data<<flags[i]<<'='<<(SendMessageW(checks[i],BM_GETCHECK,0,0)==BST_CHECKED?"true":"false")<<'\n';
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("create");
    auto bytes=data.str();DWORD written=0;
    bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!ok||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());throw std::runtime_error("save");}
    status(L"Enregistre. Cliquez Arreter puis Installer pour relire les reglages. Verifiez ensuite Etat. Une transaction d'armure non resolue bloque le rechargement.");
}
void run(const wchar_t* operation) {
    auto pid=value(pidBox);
    if(pid.empty()||pid.find_first_not_of(L"0123456789")!=std::wstring::npos||std::stoull(pid)==0||std::stoull(pid)>MAXDWORD)throw std::runtime_error("PID");
    wchar_t self[32768];GetModuleFileNameW(nullptr,self,32768);
    fs::path exe=fs::path(self).parent_path()/L"inspect_process.exe";
    if(!fs::exists(exe))throw std::runtime_error("inspector missing");
    std::wstring command=L"\""+exe.wstring()+L"\" "+pid+L" --game-control "+operation;
    busy=true;for(auto button:buttons)EnableWindow(button,FALSE);
    status(L"Commande en cours...");
    worker=std::thread([exe,command]() mutable {
        HANDLE read=nullptr,write=nullptr;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
        std::string captured;
        if(CreatePipe(&read,&write,&attributes,0)&&SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)) {
            STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
            startup.hStdOutput=write;startup.hStdError=write;
            startup.hStdInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,0,nullptr);
            PROCESS_INFORMATION process{};
            if(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&startup,&process)) {
                CloseHandle(write);write=nullptr;
                char chunk[1024];DWORD count;
                while(ReadFile(read,chunk,sizeof(chunk),&count,nullptr)&&count)if(captured.size()<65536)captured.append(chunk,count);
                WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
                captured+="\r\nCode de sortie : "+std::to_string(code);
                CloseHandle(process.hThread);CloseHandle(process.hProcess);
            } else captured="Impossible de lancer inspect_process.exe. Erreur Windows : "+std::to_string(GetLastError());
            if(startup.hStdInput!=INVALID_HANDLE_VALUE)CloseHandle(startup.hStdInput);
        } else captured="Impossible de creer le canal de diagnostic.";
        if(read)CloseHandle(read);if(write)CloseHandle(write);
        result=widen(captured);PostMessageW(window,finished,0,0);
    });
}
LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    switch(message) {
    case WM_COMMAND:
        if(HIWORD(wp)!=BN_CLICKED||busy)break;
        try {
            switch(LOWORD(wp)) {
            case 10:load();break;case 11:save();break;
            case 12:{wchar_t path[32768]{};auto existing=value(pathBox);wcsncpy_s(path,existing.c_str(),_TRUNCATE);
                OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrFilter=L"Configuration\0*.properties\0\0";dialog.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
                if(GetOpenFileNameW(&dialog))SetWindowTextW(pathBox,path);break;}
            case 20:run(L"status");break;case 21:run(L"install");break;case 22:run(L"stop");break;
            }
        }catch(...){status(L"Operation refusee : verifiez le PID, le chemin et les valeurs. Portee ]0,6], cadence entiere 20..1000 ms, seuil 0..1, delai entier 500..10000 ms. Utilisez un point decimal.");}
        return 0;
    case finished:
        worker.join();busy=false;for(auto button:buttons)EnableWindow(button,TRUE);status(result);return 0;
    case WM_CLOSE:
        if(busy){status(L"Attendez la fin de la commande avant de fermer. Aucun thread distant ne sera interrompu de force.");return 0;}
        DestroyWindow(hwnd);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,message,wp,lp);
}
int selfTest() {
    try {auto good=validate(L"6",L"20",L"0",L"10000");if(good.reach!=6||good.period!=20)return 1;
        const wchar_t* invalid[]={L"0",L"6.01",L"nan",L"inf",L"3junk",L"3,5",L"",L"-1"};
        for(auto input:invalid){bool rejected=false;try{validate(input,L"100",L"0.15",L"1500");}catch(...){rejected=true;}if(!rejected)return 2;}
        bool rejected=false;try{validate(L"3",L"20.5",L"0.15",L"1500");}catch(...){rejected=true;}if(!rejected)return 3;
        return 0;
    }catch(...){return 4;}
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR args,int show) {
    if(std::wstring(args)==L"--self-test")return selfTest();
    SetProcessDPIAware();
    WNDCLASSW klass{};klass.hInstance=instance;klass.lpszClassName=L"LunarLocalControl";klass.lpfnWndProc=procedure;klass.hCursor=LoadCursor(nullptr,IDC_ARROW);klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&klass);
    window=CreateWindowW(klass.lpszClassName,L"Lunar Local Lab | Minecraft 1.8.9",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,790,640,nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    control(L"STATIC",L"LUNAR LOCAL LAB  /  Controle C++",0,24,20,720,28);
    control(L"STATIC",L"Portee locale avec obstacles. L'acceptation des actions reste geree par le serveur.",0,24,54,730,24);
    control(L"STATIC",L"PID Java de votre instance :",0,24,95,210,24);pidBox=control(L"EDIT",L"",WS_TABSTOP|ES_NUMBER,240,92,140,28);
    auto button=[](const wchar_t* name,int x,int y,int width,int id){buttons.push_back(control(L"BUTTON",name,WS_TABSTOP|BS_PUSHBUTTON,x,y,width,30,id));};
    button(L"Etat",400,92,100,20);button(L"Installer",510,92,100,21);button(L"Arreter",620,92,120,22);
    control(L"STATIC",L"Fichier de configuration (Minecraft.mcDataDir)",0,24,138,710,24);
    wchar_t appdata[32768]{};GetEnvironmentVariableW(L"APPDATA",appdata,32768);
    auto config=fs::path(appdata)/L".minecraft"/L"lunar-local-lab.properties";
    pathBox=control(L"EDIT",config.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,24,166,600,28);button(L"Parcourir",636,166,104,12);
    const wchar_t* labels[]={L"Portee (0 < valeur <= 6)",L"Cadence (20..1000 ms)",L"Seuil armure (0..1)",L"Delai armure (500..10000 ms)"};
    const wchar_t* defaults[]={L"3.0",L"100",L"0.15",L"1500"};
    for(int i=0;i<4;++i){int x=24+(i%2)*370,y=214+(i/2)*48;control(L"STATIC",labels[i],0,x,y,255,24);inputs[i]=control(L"EDIT",defaults[i],WS_TABSTOP|ES_AUTOHSCROLL,x+260,y-3,80,28);}
    const wchar_t* flags[]={L"Clics repetes",L"Remplacement armure",L"Selection geometrique"};
    for(int i=0;i<3;++i){checks[i]=control(L"BUTTON",flags[i],WS_TABSTOP|BS_AUTOCHECKBOX,24+i*240,310,235,26);SendMessageW(checks[i],BM_SETCHECK,BST_CHECKED,0);}
    button(L"Charger le fichier",24,353,180,10);button(L"Enregistrer",220,353,180,11);
    control(L"STATIC",L"Appliquer : Arreter puis Installer. En jeu : F8 armer/desarmer, F9 arreter.\nSolo et serveurs acceptes. Fermer cette fenetre n'arrete pas le pont.",0,24,400,720,48);
    output=control(L"EDIT",L"Pret. Choisissez le PID et verifiez le chemin. Installer charge le pont JNI dans cette instance.",ES_MULTILINE|ES_READONLY|WS_VSCROLL,24,465,716,115);
    ShowWindow(window,show);UpdateWindow(window);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    DeleteObject(font);return 0;
}
