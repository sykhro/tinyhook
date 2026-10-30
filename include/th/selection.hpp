#pragma once
#include "protocol.hpp"

namespace th {
    constexpr std::uint8_t selection_listener_role=5,selection_request_role=6;

    inline Bytes encode_selection(std::string_view title) {
        if(title.empty()||title.size()>1024||title.find('\0')!=title.npos||!valid_utf8(title))
            throw Error("window title must be nonempty UTF-8, at most 1024 bytes");
        Bytes b;
        text(b,title);
        return b;
    }

    inline std::string decode_selection(std::span<const std::uint8_t> bytes) {
        Reader r(bytes);
        auto title=r.string(1024);
        r.end();
        (void)encode_selection(title);
        return title;
    }

    struct SelectionResult {
        bool success;
        std::string message;
    };

    inline Bytes encode_selection_result(const SelectionResult& result) {
        if(result.message.size()>4096||!valid_utf8(result.message))throw Error("invalid selection result");
        Bytes b;
        u8(b,result.success?1:0);
        text(b,result.message);
        return b;
    }

    inline SelectionResult decode_selection_result(std::span<const std::uint8_t> bytes) {
        Reader r(bytes);
        auto status=r.get8();
        if(status>1)throw Error("invalid selection status");
        SelectionResult result{status==1,r.string(4096)};
        r.end();
        (void)encode_selection_result(result);
        return result;
    }
}
