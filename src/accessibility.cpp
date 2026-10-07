#include "accessibility.h"
namespace tempos {
HRESULT WidgetAccessibility::QueryInterface(REFIID iid, void **out) {
  if (!out)
    return E_POINTER;
  *out = nullptr;
  if (iid == IID_IUnknown || iid == __uuidof(IRawElementProviderSimple))
    *out = static_cast<IRawElementProviderSimple *>(this);
  else if (iid == __uuidof(IInvokeProvider))
    *out = static_cast<IInvokeProvider *>(this);
  else
    return E_NOINTERFACE;
  AddRef();
  return S_OK;
}
ULONG WidgetAccessibility::AddRef() {
  return ++refs_;
}
ULONG WidgetAccessibility::Release() {
  ULONG n = --refs_;
  if (!n)
    delete this;
  return n;
}
HRESULT WidgetAccessibility::get_ProviderOptions(ProviderOptions *out) {
  if (!out)
    return E_POINTER;
  *out = ProviderOptions_ServerSideProvider;
  return S_OK;
}
HRESULT WidgetAccessibility::GetPatternProvider(PATTERNID id, IUnknown **out) {
  if (!out)
    return E_POINTER;
  *out = nullptr;
  if (id == UIA_InvokePatternId) {
    *out = static_cast<IInvokeProvider *>(this);
    AddRef();
  }
  return S_OK;
}
HRESULT WidgetAccessibility::GetPropertyValue(PROPERTYID id, VARIANT *out) {
  if (!out)
    return E_POINTER;
  VariantInit(out);
  std::lock_guard g(mutex_);
  if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId || id == UIA_HelpTextPropertyId) {
    out->vt = VT_BSTR;
    out->bstrVal = SysAllocString(id == UIA_NamePropertyId           ? name_.c_str()
                                  : id == UIA_AutomationIdPropertyId ? id_.c_str()
                                                                     : L"조회 위젯. Enter로 설정을 엽니다.");
  } else if (id == UIA_ControlTypePropertyId) {
    out->vt = VT_I4;
    out->lVal = UIA_CustomControlTypeId;
  } else if (id == UIA_IsEnabledPropertyId || id == UIA_IsKeyboardFocusablePropertyId ||
             id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
    out->vt = VT_BOOL;
    out->boolVal = VARIANT_TRUE;
  }
  return S_OK;
}
HRESULT WidgetAccessibility::get_HostRawElementProvider(IRawElementProviderSimple **out) {
  std::lock_guard g(mutex_);
  if (!hwnd_)
    return UIA_E_ELEMENTNOTAVAILABLE;
  return UiaHostProviderFromHwnd(hwnd_, out);
}
HRESULT WidgetAccessibility::Invoke() {
  std::lock_guard g(mutex_);
  if (!hwnd_)
    return UIA_E_ELEMENTNOTAVAILABLE;
  PostMessageW(hwnd_, WM_TEMPOS_SETTINGS, 0, 0);
  return S_OK;
}
void WidgetAccessibility::update(std::wstring name) {
  std::lock_guard g(mutex_);
  name_ = std::move(name);
}
void WidgetAccessibility::disconnect() {
  {
    std::lock_guard g(mutex_);
    hwnd_ = nullptr;
  }
  UiaDisconnectProvider(this);
}
} // namespace tempos
