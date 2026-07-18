int remaining_bits(WmaProState* s, GetBits& gb) {
    return s->buf_bit_size - gb.tell();
}

void copy_bits_to_pb(WmaProState* s, GetBits& gb, int len) {
    while (len >= 24) {
        s->pb.write(24, gb.read(24));
        len -= 24;
    }
    while (len >= 8) {
        s->pb.write(8, gb.read(8));
        len -= 8;
    }
    if (len > 0) s->pb.write(len, gb.read(len));
}

void save_bits(WmaProState* s, GetBits& gb, int len, int append) {
    int buflen;
    if (!append) {
        s->frame_offset   = gb.tell() & 7;
        s->num_saved_bits = s->frame_offset;
        s->pb.init(s->frame_data.data(), MAX_FRAMESIZE);
        buflen = (s->num_saved_bits + len + 7) >> 3;
    } else {
        buflen = (s->pb.tell() + len + 7) >> 3;
    }
    if (len <= 0 || buflen > MAX_FRAMESIZE) {
        s->packet_loss = 1;
        return;
    }
    s->num_saved_bits += len;
    if (!append) {

        s->pb.write(s->frame_offset, 0);
        copy_bits_to_pb(s, gb, len);
    } else {
        int align = 8 - (gb.tell() & 7);
        align = std::min(align, len);
        s->pb.write(align, gb.read(align));
        len -= align;
        copy_bits_to_pb(s, gb, len);
    }
    s->pb.flush();
    s->gb.init(s->frame_data.data(), (s->num_saved_bits + 7) >> 3);
    s->gb.skip(s->frame_offset);
}
