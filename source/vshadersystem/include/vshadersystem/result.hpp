#pragma once

#include <string>
#include <utility>

namespace vshadersystem
{
    enum class ErrorCode : uint32_t
    {
        eOk = 0,
        eIO,
        eInvalidArgument,
        eParseError,
        eCompileError,
        eReflectError,
        eSerializeError,
        eDeserializeError
    };

    struct Error
    {
        ErrorCode   code = ErrorCode::eOk;
        std::string message;

        static Error ok() { return {ErrorCode::eOk, {}}; }
    };

    template<typename T>
    class Result
    {
    public:
        static Result ok(T value)
        {
            Result r;
            r.m_Ok    = true;
            r.m_Value = std::move(value);
            return r;
        }

        static Result err(Error e)
        {
            Result r;
            r.m_Ok    = false;
            r.m_Error = std::move(e);
            return r;
        }

        bool         isOk() const { return m_Ok; }
        const T&     value() const { return m_Value; }
        T&           value() { return m_Value; }
        const Error& error() const { return m_Error; }

    private:
        bool  m_Ok = false;
        T     m_Value {};
        Error m_Error {};
    };

    template<>
    class Result<void>
    {
    public:
        static Result ok()
        {
            Result r;
            r.m_Ok = true;
            return r;
        }

        static Result err(Error e)
        {
            Result r;
            r.m_Ok    = false;
            r.m_Error = std::move(e);
            return r;
        }

        bool         isOk() const { return m_Ok; }
        const Error& error() const { return m_Error; }

    private:
        bool  m_Ok = false;
        Error m_Error {};
    };
} // namespace vshadersystem
