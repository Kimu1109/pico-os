#pragma once

#include "widgets/Widget.hpp"

class ScrollContainer : public Widget {
    private:
        std::vector<Widget*> children_;

    public:
        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }
};