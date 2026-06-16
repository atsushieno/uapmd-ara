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
#if ANDROID
        // FIXME: implement
        //if (auto binding = createAapAraBinding(*native))
        //    return binding;
#else
        if (auto binding = createVst3AraBinding(*native))
            return binding;
        if (auto binding = createClapAraBinding(*native))
            return binding;
        if (auto binding = createAudioUnitAraBinding(*native))
            return binding;
#endif
        return nullptr;
    }

} // namespace uapmd::ara
