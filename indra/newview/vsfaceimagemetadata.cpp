// Shared Face image metadata follows the real fetch/composite/media owner. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsfaceimagemetadata.h"
#include "llviewerobject.h"
#include "llimage.h"
#if VS_NATIVE_VULKAN
#include "vsmaterialselection.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "vsuiimageprovider.h"
#include "llviewermedia.h"
#include "llpluginclassmedia.h"
#endif
LLGLenum VSFaceImageMetadata::getPrimaryFormat()const
{
    if(mLegacy)return mLegacy->getPrimaryFormat();
    if(mSourceFormat)return mSourceFormat;
    // Same component-to-format mapping as LLImageGL::createGLTexture; zero
    // remains unknown while the genuine decoder has not published metadata.
    switch(mComponents)
    {
    case 1:return GL_LUMINANCE;
    case 2:return GL_LUMINANCE_ALPHA;
    case 3:return GL_RGB;
    case 4:return GL_RGBA;
    default:return 0;
    }
}
VSFaceImageMetadata vs_face_image(LLViewerObject* object,S32 side)
{
    VSFaceImageMetadata value;
    if(object && side>=0 && side<object->getNumTEs())
    {
        value.mLegacy=object->getTEImage(side);value.mPresent=value.mLegacy.notNull();
    }
    return value;
}
#if VS_NATIVE_VULKAN
VSFaceImageMetadata vs_face_image(const VSMaterialObject* object,S32 side)
{
    VSFaceImageMetadata value;
    if(!object || !*object || side<0 || side>=object->getNumTEs() || !gViewerWindow || !gViewerWindow->nativeContext())return value;
    const auto image=gViewerWindow->nativeContext()->world().diffuseTexture(object->task().nativeObject(),U32(side),false);
    value.mPresent=image.present;value.mMissing=image.missing;value.mID=image.asset;
    value.mComponents=image.asset.notNull()?vs_ui_image_components(image.asset):image.raw?image.raw->getComponents():0;
    // Media publishes normalized RGBA storage, while the original Face controls
    // inspect the plugin's actual primary format (including BGRA), not storage.
    if(image.asset.notNull())
        if(const auto media=LLViewerMedia::getInstance()->getMediaImplFromTextureID(image.asset))
            if(const auto* plugin=media->getMediaPlugin())value.mSourceFormat=plugin->getTextureFormatPrimary();
    return value;
}
#endif
