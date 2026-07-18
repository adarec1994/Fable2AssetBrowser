WmaProDecoder::WmaProDecoder() : impl_(std::make_unique<Impl>()) {}
WmaProDecoder::~WmaProDecoder() = default;

int WmaProDecoder::init(CodecContext& ctx) { return impl_->init(ctx); }

int WmaProDecoder::decode_packet(const Packet& pkt, Frame& out, bool& got_frame) {
    return impl_->decode_packet_impl(pkt, out, got_frame);
}

int WmaProDecoder::flush(Frame& out, bool& got_frame) {
    return impl_->flush_impl(out, got_frame);
}

bool WmaProDecoder::packet_done() const {
    const auto& st = impl_->x.xma[impl_->is_xma ? impl_->x.current_stream : 0];
    return st.packet_done != 0;
}

bool WmaProDecoder::packet_loss() const {
    const auto& st = impl_->x.xma[impl_->is_xma ? impl_->x.current_stream : 0];
    return st.packet_loss != 0;
}
