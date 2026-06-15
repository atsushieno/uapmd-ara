#include "uapmd-ara/uapmd-ara.hpp"

#include "AraFormatBinding.hpp"
#include "AraHostDocumentController.hpp"

#include <map>
#include <stdexcept>

namespace uapmd::ara {

    namespace {
        class AraSupportImpl final
            : public AraSupport
            , public ProjectDocumentEventListener {
            struct NativeAraDocument {
                std::unique_ptr<AraFormatBinding> binding{};
                std::unique_ptr<AraHostDocumentController> controller{};
                const ARA::ARAPlugInExtensionInstance* plugin_extension{};
            };

            SequencerEngine& engine_;
            std::unique_ptr<AraSession> session_;
            ProjectDocumentEventSource* event_source_{};
            ProjectDocumentEventListenerToken event_listener_token_{};
            std::map<int32_t, NativeAraDocument> native_ara_documents_{};

            void resyncNativeAraDocuments() {
                for (auto& [pluginInstanceId, document] : native_ara_documents_) {
                    (void) pluginInstanceId;
                    if (document.controller)
                        document.controller->resyncFromProjectDocument(engine_.timeline().projectDocumentView());
                }
            }

        public:
            explicit AraSupportImpl(SequencerEngine& engine)
                : engine_(engine)
                , session_(AraSession::create()) {
                if (!session_)
                    throw std::runtime_error("Failed to create ARA session.");

                auto status = session_->bindProjectDocument(
                    engine_.timeline().projectDocumentView(),
                    engine_.timeline().projectDocumentEvents());
                if (status != AraStatus::Ok)
                    throw std::runtime_error("Failed to bind ARA session to project document.");

                event_source_ = &engine_.timeline().projectDocumentEvents();
                event_listener_token_ = event_source_->addProjectDocumentEventListener(*this);
            }

            ~AraSupportImpl() override {
                if (event_source_ && event_listener_token_ != 0)
                    event_source_->removeProjectDocumentEventListener(event_listener_token_);
                if (session_)
                    session_->unbindProjectDocument();
            }

            AraSession& session() override {
                return *session_;
            }

            const AraSession& session() const override {
                return *session_;
            }

            AraStatus attachPlugin(
                int32_t pluginInstanceId,
                AudioPluginInstanceAPI& pluginInstance) override {
                auto status = session_->attachPlugin(pluginInstanceId, pluginInstance);
                if (status != AraStatus::UnsupportedPlugin)
                    return status;

                auto nativeBinding = createAraFormatBinding(pluginInstance);
                if (!nativeBinding)
                    return AraStatus::UnsupportedPlugin;

                auto controller = std::make_unique<AraHostDocumentController>(
                    *nativeBinding->factory(),
                    "uapmd");
                if (!controller->valid())
                    return AraStatus::BackendError;

                if (!controller->resyncFromProjectDocument(engine_.timeline().projectDocumentView()))
                    return AraStatus::BackendError;

                const auto knownRoles =
                    ARA::kARAPlaybackRendererRole |
                    ARA::kARAEditorRendererRole |
                    ARA::kARAEditorViewRole;
                auto* pluginExtension = nativeBinding->bindToDocumentController(
                    controller->documentControllerRef(),
                    knownRoles,
                    knownRoles);
                if (!pluginExtension)
                    return AraStatus::BackendError;

                native_ara_documents_.emplace(
                    pluginInstanceId,
                    NativeAraDocument{
                        .binding = std::move(nativeBinding),
                        .controller = std::move(controller),
                        .plugin_extension = pluginExtension
                    });
                return AraStatus::Ok;
            }

            void detachPlugin(int32_t pluginInstanceId) override {
                native_ara_documents_.erase(pluginInstanceId);
                session_->detachPlugin(pluginInstanceId);
            }

            bool hasNativeAraBinding(int32_t pluginInstanceId) const override {
                return native_ara_documents_.contains(pluginInstanceId);
            }

            const ARA::ARAFactory* nativeAraFactory(int32_t pluginInstanceId) const override {
                auto it = native_ara_documents_.find(pluginInstanceId);
                if (it == native_ara_documents_.end())
                    return nullptr;
                return it->second.binding->factory();
            }

            const ARA::ARAPlugInExtensionInstance* bindNativeAraPlugin(
                int32_t pluginInstanceId,
                ARA::ARADocumentControllerRef documentControllerRef,
                ARA::ARAPlugInInstanceRoleFlags knownRoles,
                ARA::ARAPlugInInstanceRoleFlags assignedRoles) override {
                auto it = native_ara_documents_.find(pluginInstanceId);
                if (it == native_ara_documents_.end())
                    return nullptr;
                if (!documentControllerRef)
                    return it->second.plugin_extension;
                return it->second.binding->bindToDocumentController(documentControllerRef, knownRoles, assignedRoles);
            }

            void projectLoaded(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void projectClosing(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void masterTrackChanged(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void trackAdded(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void trackRemoved(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void trackChanged(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void clipAdded(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void clipRemoved(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void clipChanged(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void audioSourceAdded(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void audioSourceRemoved(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }

            void audioSourceChanged(const ProjectDocumentEvent& event) override {
                (void) event;
                resyncNativeAraDocuments();
            }
        };
    } // namespace

    std::unique_ptr<AraSupport> createAraSupport(SequencerEngine& engine) {
        return std::make_unique<AraSupportImpl>(engine);
    }

} // namespace uapmd::ara
