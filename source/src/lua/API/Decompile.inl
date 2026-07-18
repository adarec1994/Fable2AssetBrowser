std::string decompile(const Function& f) {
    Decompiler dec(&f);
    dec.decompile();
    return dec.out.str();
}
