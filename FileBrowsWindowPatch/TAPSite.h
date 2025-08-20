#pragma once  
#include <ocidl.h>  
#include <xamlOM.h>  
#include <winrt/base.h>  
#include <wil/resource.h>  

extern const GUID CLSID_TAPSite;

// ǰ������  
struct VisualTreeWatcher;

class TAPSite : public winrt::implements<TAPSite, IObjectWithSite, winrt::non_agile>
{
public:
    static wil::unique_event_nothrow GetReadyEvent();
    static DWORD WINAPI Install(void* parameter);
    static DWORD WINAPI InstallUdk(void* parameter);

private:
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* pUnkSite) override;
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppvSite) noexcept override;

    static winrt::com_ptr<IVisualTreeServiceCallback2> s_VisualTreeWatcher;
    winrt::com_ptr<IUnknown> site;
};