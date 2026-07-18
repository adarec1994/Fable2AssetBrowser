bool CompileShader(const char* source,
                   const char* entry,
                   const char* profile,
                   ID3DBlob** blob)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* errors = nullptr;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), nullptr, nullptr, nullptr,
        entry, profile, flags, 0, blob, &errors);
    if (FAILED(result) && errors) {
        const char* message =
            static_cast<const char*>(errors->GetBufferPointer());
        OutputLog::error(std::string("shader compile failed ") +
            profile + "/" + entry + ": " +
            (message ? message : "unknown error"));
    }
    if (errors) errors->Release();
    return SUCCEEDED(result);
}
