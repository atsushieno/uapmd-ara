#include "AraFormatBinding.hpp"

namespace uapmd::ara {

    namespace {
        NativePluginInstanceHandleExtension* nativeHandleExtension(AudioPluginInstanceAPI& pluginInstance) {
            auto* extension = pluginInstance.extension(kNativePluginInstanceHandleExtensionId);
            return dynamic_cast<NativePluginInstanceHandleExtension*>(extension);
        }
    }

    std::unique_ptr<AraFormatBinding> createAraFormatBinding(AudioPluginInstanceAPI& pluginInstance) {
        auto* native = nativeHandleExtension(pluginInstance);
        if (!native)
            return nullptr;

        if (auto binding = createVst3AraBinding(*native))
            return binding;
        if (auto binding = createClapAraBinding(*native))
            return binding;
        if (auto binding = createAudioUnitAraBinding(*native))
            return binding;
        return nullptr;
    }

} // namespace uapmd::ara
