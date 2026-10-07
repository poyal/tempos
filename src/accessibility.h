#pragma once
#include "model.h"
#include <atomic>
#include <uiautomation.h>
namespace tempos {
class WidgetAccessibility final : public IRawElementProviderSimple, public IInvokeProvider {
public:
  WidgetAccessibility(HWND hwnd, std::wstring id) : hwnd_(hwnd), id_(std::move(id)) {}
  void update(std::wstring name);
  void disconnect();
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override;
  ULONG STDMETHODCALLTYPE AddRef() override;
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *) override;
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown **) override;
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID, VARIANT *) override;
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **) override;
  HRESULT STDMETHODCALLTYPE Invoke() override;

private:
  std::atomic<ULONG> refs_{1};
  std::mutex mutex_;
  HWND hwnd_;
  std::wstring id_, name_;
};
constexpr UINT WM_TEMPOS_SETTINGS = WM_APP + 4;
} // namespace tempos
