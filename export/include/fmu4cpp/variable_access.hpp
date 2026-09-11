
#ifndef FMU4CPP_VARIABLEACCESS_HPP
#define FMU4CPP_VARIABLEACCESS_HPP

#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace fmu4cpp {

    template<typename T>
    struct VariableAccess {
        virtual T get() = 0;
        virtual void set(T value) = 0;

        virtual void read(T *dest, size_t count) {
            if (count == 1) {
                dest[0] = get();
            } else {
                throw std::runtime_error("Array read not supported for this access type");
            }
        }

        virtual void write(const T *src, size_t count) {
            if (count == 1) {
                set(src[0]);
            } else {
                throw std::runtime_error("Array write not supported for this access type");
            }
        }

        virtual ~VariableAccess() = default;
    };

    template<typename T>
    class PtrAccess final : public VariableAccess<T> {

    public:
        explicit PtrAccess(T *ptr, const std::function<void()> &onChange)
            : ptr_(ptr), onChange_(onChange) {}

        T get() override {
            return *ptr_;
        }

        void set(T value) override {
            *ptr_ = value;
            if (onChange_) onChange_();
        }

        void read(T *dest, size_t count) override {
            if (count == 1) {
                dest[0] = *ptr_;
            } else {
                std::copy(ptr_, ptr_ + count, dest);
            }
        }

        void write(const T *src, size_t count) override {
            if (count == 1) {
                *ptr_ = src[0];
            } else {
                std::copy(src, src + count, ptr_);
            }
            if (onChange_) onChange_();
        }

    private:
        T *ptr_;
        std::function<void()> onChange_;
    };

    template<typename T>
    class LambdaAccess final : public VariableAccess<T> {

    public:
        LambdaAccess(std::function<T()> getter, std::optional<std::function<void(T)>> setter)
            : getter_(std::move(getter)),
              setter_(std::move(setter)) {}

        T get() override {
            return getter_();
        }

        void set(T value) override {
            if (setter_) {
                setter_->operator()(value);
            }
        }

        void read(T *dest, size_t count) override {
            if (count == 1) {
                dest[0] = getter_();
            } else {
                throw std::runtime_error("Array read not supported for scalar lambda");
            }
        }

        void write(const T *src, size_t count) override {
            if (count == 1) {
                if (setter_) {
                    (*setter_)(src[0]);
                }
            } else {
                throw std::runtime_error("Array write not supported for scalar lambda");
            }
        }

    private:
        std::function<T()> getter_;
        std::optional<std::function<void(T)>> setter_;
    };

}// namespace fmu4cpp

#endif//FMU4CPP_VARIABLEACCESS_HPP
