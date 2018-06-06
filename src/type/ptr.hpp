#pragma once

#include <memory>
// pointer wrapper
template<class T> using ptrS = std::shared_ptr<T>;
template<class T> using ptrU = std::unique_ptr<T>;
template<class T> using ptrW = std::weak_ptr<T>;

#define mv std::move
