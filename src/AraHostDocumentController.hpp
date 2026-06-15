#pragma once

#include <string>

#include <ARA_API/ARAInterface.h>
#include <uapmd-data/uapmd-data.hpp>

namespace uapmd::ara {

    class AraHostDocumentController {
    public:
        struct Impl;

        explicit AraHostDocumentController(const ARA::ARAFactory& factory, std::string documentName);
        ~AraHostDocumentController();

        AraHostDocumentController(const AraHostDocumentController&) = delete;
        AraHostDocumentController& operator=(const AraHostDocumentController&) = delete;

        bool valid() const;
        ARA::ARADocumentControllerRef documentControllerRef() const;
        const ARA::ARAFactory* factory() const;
        bool resyncFromProjectDocument(ProjectDocumentView& documentView);
        void notifyModelUpdates();

    private:
        Impl* impl_{};
    };

} // namespace uapmd::ara
