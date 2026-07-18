bool MP_Init(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    if(!create_target(dev, mp, w, h)) return false;
    if(!create_pipeline(dev, mp)) return false;
    return true;
}
void MP_Resize(ID3D11Device* dev, ModelPreview& mp, int w, int h){
    if(w == mp.width && h == mp.height) return;
    create_target(dev, mp, w, h);
}
void MP_Release(ModelPreview& mp){
    mp_release(mp);
}
