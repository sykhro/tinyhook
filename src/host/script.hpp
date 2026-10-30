#pragma once
#include "th/protocol.hpp"
#include <memory>
#include <optional>

namespace th {
    class Script {
        struct Impl;

        std::unique_ptr<Impl> impl_;

        std::vector<Spec> specs_;
        std::string id_,engine_;
        std::vector<std::string> processes_;

        public:
        explicit Script(const std::string& filename);

        ~Script();

        Script(const Script&)=delete;

        Script& operator=(const Script&)=delete;

        const std::vector<Spec>& specs() const {
            return specs_;
        }

        const std::string& id() const { return id_; }
        const std::string& engine() const { return engine_; }
        const std::vector<std::string>& processes() const { return processes_; }

        bool supports(Architecture arch) const;
        ProcessPriority process_priority(std::string_view process) const;

        std::optional<std::string> apply(const Event& event);
    };
}
