#ifndef EXPORTERERROR_H
#define EXPORTERERROR_H

namespace Export
{
    class ExporterError final : public std::logic_error
    {
    public:
        explicit inline ExporterError(const char *Message)
            : std::logic_error(Message)
        {}
    };
}

#endif // EXPORTERERROR_H
