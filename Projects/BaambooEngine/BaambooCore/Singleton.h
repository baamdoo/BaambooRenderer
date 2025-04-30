#pragma once

template< typename T >
class Singleton
{
protected:
    Singleton() = default;

public:
    Singleton(const Singleton& c) = delete;
    Singleton(Singleton&& c) = delete;
    void operator=(const Singleton& c) = delete;
    void operator=(Singleton&& c) = delete;

    // return type should be pointer since if the instance
    // is called in a function, the destructor should be called
    // at the end point of the function.
    static T* Inst()
    {
        static T instance;
        return &instance;
    }
};