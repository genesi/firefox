/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenuObject.h"
#include "nsUnicharUtils.h"
#include "nsIDocument.h"
#include "nsIPresShell.h"
#include "nsComputedDOMStyle.h"
#include "nsStyleContext.h"
#include "nsStyleStruct.h"
#include "nsStyleConsts.h"
#include "nsGkAtoms.h"
#include "mozilla/LookAndFeel.h"
#include "nsIAtom.h"
#include "nsString.h"
#include "nsIRunnable.h"
#include "nsThreadUtils.h"
#include "nsAttrValue.h"
#include "mozilla/dom/Element.h"
#include "nsILoadGroup.h"
#include "nsContentUtils.h"
#include "imgLoader.h"
#include "imgRequestProxy.h"
#include "imgIRequest.h"
#include "nsPresContext.h"
#include "imgIContainer.h"
#include "nsImageToPixbuf.h"
#include "mozilla/Util.h"
#include "nsServiceManagerUtils.h"
#include "nsNetUtil.h"
#include "mozilla/Preferences.h"
#include "ImageOps.h"

#include "nsNativeMenuUtils.h"
#include "nsMenu.h"
#include "nsMenuItem.h"
#include "nsMenuSeparator.h"
#include "nsNativeMenuDocListener.h"
#include "nsMenuBar.h"
#include "nsNativeMenuAtoms.h"

#include <glib-object.h>
#include <gdk/gdk.h>
#include <pango/pango.h>

using namespace mozilla;
using mozilla::image::ImageOps;

#define MAX_WIDTH 350000

// Must be kept in sync with nsMenuObjectPropertyFlags
const char *property_strings[] = {
  DBUSMENU_MENUITEM_PROP_LABEL,
  DBUSMENU_MENUITEM_PROP_ENABLED,
  DBUSMENU_MENUITEM_PROP_VISIBLE,
  DBUSMENU_MENUITEM_PROP_ICON_DATA,
  DBUSMENU_MENUITEM_PROP_TYPE,
  DBUSMENU_MENUITEM_PROP_SHORTCUT,
  DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
  DBUSMENU_MENUITEM_PROP_TOGGLE_STATE,
  DBUSMENU_MENUITEM_PROP_CHILD_DISPLAY,
  NULL
};

const nsMenuObjectContainer::storage_type::index_type nsMenuObjectContainer::NoIndex = nsMenuObjectContainer::storage_type::NoIndex;

static enum {
    eImagesInMenus_Unknown,
    eImagesInMenus_Hide,
    eImagesInMenus_Show
} gImagesInMenus = eImagesInMenus_Unknown;

PangoLayout* gPangoLayout = nullptr;

NS_IMPL_ISUPPORTS1(nsMenuObject::IconLoader, imgINotificationObserver)

NS_IMETHODIMP
nsMenuObject::IconLoader::Notify(imgIRequest *aProxy,
                                 int32_t aType, const nsIntRect *aRect)
{
    if (!mOwner) {
        return NS_OK;
    }

    if (aProxy != mImageRequest) {
        return NS_ERROR_FAILURE;
    }

    if (aType == imgINotificationObserver::DECODE_COMPLETE) {
        mImageRequest->Cancel(NS_BINDING_ABORTED);
        mImageRequest = nullptr;
        return NS_OK;
    }

    if (aType != imgINotificationObserver::FRAME_COMPLETE) {
        return NS_OK;
    }

    if (mIconLoaded) {
        return NS_OK;
    }

    mIconLoaded = true;

    nsCOMPtr<imgIContainer> img;
    mImageRequest->GetImage(getter_AddRefs(img));
    if (!img) {
        return NS_ERROR_FAILURE;
    }

    if (!mImageRect.IsEmpty()) {
        img = ImageOps::Clip(img, mImageRect);
    }

    int32_t width, height;
    img->GetWidth(&width);
    img->GetHeight(&height);

    if (width <= 0 || height <= 0) {
        mOwner->ClearIcon();
        return NS_OK;
    }

    if (width > 100 || height > 100) {
        // The icon data needs to go across DBus. Make sure the icon
        // data isn't too large, else our connection gets terminated and
        // GDbus helpfully aborts the application. Thank you :)
        NS_WARNING("Icon data too large");
        mOwner->ClearIcon();
        return NS_OK;
    }

    GdkPixbuf *pixbuf = nsImageToPixbuf::ImageToPixbuf(img);
    if (pixbuf) {
        dbusmenu_menuitem_property_set_image(mOwner->GetNativeData(),
                                             DBUSMENU_MENUITEM_PROP_ICON_DATA,
                                             pixbuf);
        g_object_unref(pixbuf);
    }

    return NS_OK;
}

void
nsMenuObject::IconLoader::ScheduleIconLoad()
{
    nsCOMPtr<nsIRunnable> event =
        NS_NewRunnableMethod(this, &nsMenuObject::IconLoader::LoadIcon);
    NS_DispatchToCurrentThread(event);
}

void
nsMenuObject::IconLoader::LoadIcon()
{
    if (!mOwner) {
        // Our menuitem has gone away
        return;
    }

    nsIDocument *doc = mOwner->ContentNode()->OwnerDoc();

    nsCOMPtr<nsIURI> uri;
    nsIntRect imageRect;
    imgRequestProxy *imageRequest = nullptr;

    nsAutoString uriString;
    if (mOwner->ContentNode()->GetAttr(kNameSpaceID_None, nsGkAtoms::image,
                                       uriString)) {
        NS_NewURI(getter_AddRefs(uri), uriString);
    } else {
        nsIPresShell *shell = doc->GetShell();
        if (!shell) {
            return;
        }

        nsPresContext *pc = shell->GetPresContext();
        nsRefPtr<nsStyleContext> sc =
            nsComputedDOMStyle::GetStyleContextForElementNoFlush(
                mOwner->ContentNode()->AsElement(), nullptr, shell);
        if (!pc || !sc) {
            return;
        }

        const nsStyleList *list = sc->StyleList();
        imageRequest = list->GetListStyleImage();
        if (imageRequest) {
            imageRequest->GetURI(getter_AddRefs(uri));
            imageRect = list->mImageRegion.ToNearestPixels(
                            pc->AppUnitsPerDevPixel());
        }
    }

    if (!uri) {
        mOwner->ClearIcon();
        mURI = nullptr;

        if (mImageRequest) {
            mImageRequest->Cancel(NS_BINDING_ABORTED);
            mImageRequest = nullptr;
        }

        return;
    }

    bool same;
    if (mURI && NS_SUCCEEDED(mURI->Equals(uri, &same)) && same && 
        (!imageRequest || imageRect == mImageRect)) {
        return;
    }

    if (mImageRequest) {
        mImageRequest->Cancel(NS_BINDING_ABORTED);
        mImageRequest = nullptr;
    }

    mIconLoaded = false;

    mURI = uri;

    if (imageRequest) {
        mImageRect = imageRect;
        imageRequest->Clone(this, getter_AddRefs(mImageRequest));
    } else {
        mImageRect.SetEmpty();
        nsCOMPtr<nsILoadGroup> loadGroup = doc->GetDocumentLoadGroup();
        nsRefPtr<imgLoader> loader =
            nsContentUtils::GetImgLoaderForDocument(doc);
        if (!loader || !loadGroup) {
            NS_WARNING("Failed to get loader or load group for image load");
            return;
        }

        loader->LoadImage(uri, nullptr, nullptr, nullptr, loadGroup, this,
                          nullptr, nsIRequest::LOAD_NORMAL, nullptr,
                          nullptr, getter_AddRefs(mImageRequest));
    }

    if (!mIconLoaded) {
        if (!mImageRequest) {
            NS_WARNING("Failed to load icon");
            return;
        }

        mImageRequest->RequestDecode();
    }
}

void
nsMenuObject::IconLoader::Destroy()
{
    if (mImageRequest) {
        mImageRequest->CancelAndForgetObserver(NS_BINDING_ABORTED);
        mImageRequest = nullptr;
    }

    mOwner = nullptr;
}

typedef already_AddRefed<nsMenuObject> (*nsMenuObjectConstructor)(nsMenuObjectContainer*,
                                                                  nsIContent*);
struct nsMenuObjectConstructorMapEntry {
  nsIAtom **tag;
  nsMenuObjectConstructor constructor;
};

static const nsMenuObjectConstructorMapEntry kConstructorMap[] = {
  { &nsGkAtoms::menuitem,      nsMenuItem::Create      },
  { &nsGkAtoms::menu,          nsMenu::Create          },
  { &nsGkAtoms::menuseparator, nsMenuSeparator::Create }
};

static nsMenuObjectConstructor
GetMenuObjectConstructor(nsIContent *aContent)
{
    if (!aContent->IsXUL()) {
        return nullptr;
    }

    for (uint32_t i = 0; i < ArrayLength(kConstructorMap); ++i) {
        if (aContent->Tag() == *kConstructorMap[i].tag) {
            return kConstructorMap[i].constructor;
        }
    }

    return nullptr;
}

static bool
ContentIsSupported(nsIContent *aContent)
{
    return GetMenuObjectConstructor(aContent) ? true : false;
}

static int
CalculateTextWidth(const nsAString& aText)
{
    if (!gPangoLayout) {
        PangoFontMap *fontmap = pango_cairo_font_map_get_default();
        PangoContext *ctx = pango_font_map_create_context(fontmap);
        gPangoLayout = pango_layout_new(ctx);
        g_object_unref(ctx);
    }

    pango_layout_set_text(gPangoLayout, NS_ConvertUTF16toUTF8(aText).get(), -1);

    int width, dummy;
    pango_layout_get_size(gPangoLayout, &width, &dummy);

    return width;
}

static const nsDependentString
GetEllipsis()
{
    static PRUnichar sBuf[4] = { 0, 0, 0, 0 };
    if (!sBuf[0]) {
        nsAdoptingString ellipsis = Preferences::GetLocalizedString("intl.ellipsis");
        if (!ellipsis.IsEmpty()) {
            uint32_t l = ellipsis.Length();
            const nsAdoptingString::char_type *c = ellipsis.BeginReading();
            uint32_t i = 0;
            while (i < 3 && i < l) {
                sBuf[i++] = *(c++);
            }
        } else {
            sBuf[0] = '.';
            sBuf[1] = '.';
            sBuf[2] = '.';
        }
    }

    return nsDependentString(sBuf);
}

static int
GetEllipsisWidth()
{
    static int sEllipsisWidth = -1;

    if (sEllipsisWidth == -1) {
        sEllipsisWidth = CalculateTextWidth(GetEllipsis());
    }

    return sEllipsisWidth;
}


void
nsMenuObject::RemoveUnsupportedProperties()
{
    PropertyFlags supported = SupportedProperties();
    PropertyFlags mask = PropertyFlags(1);

    for (uint32_t i = 0; property_strings[i]; ++i) {
        if (!(mask & supported)) {
            dbusmenu_menuitem_property_remove(mNativeData, property_strings[i]);
        }
        mask = PropertyFlags(mask << 1);
    }
}

bool
nsMenuObject::ShouldShowIcon() const
{
    // Ideally we want to know the visibility of the anonymous XUL image in
    // our menuitem, but this isn't created because we don't have a frame.
    // The following works by default (because xul.css hides images in menuitems
    // that don't have the "menuitem-with-favicon" class, when eIntID_ImagesInMenus
    // is false). It's possible a third party theme could override this, but,
    // oh well...
    if (gImagesInMenus == eImagesInMenus_Unknown) {
        gImagesInMenus = LookAndFeel::GetInt(LookAndFeel::eIntID_ImagesInMenus) ?
            eImagesInMenus_Show : eImagesInMenus_Hide;
    }

    if (gImagesInMenus == eImagesInMenus_Show) {
        return true;
    }

    const nsAttrValue *classes = mContent->GetClasses();
    if (!classes) {
        return false;
    }

    for (uint32_t i = 0; i < classes->GetAtomCount(); ++i) {
        if (classes->AtomAt(i) == nsNativeMenuAtoms::menuitem_with_favicon) {
            return true;
        }
    }

    return false;
}

void
nsMenuObject::ClearIcon()
{
    dbusmenu_menuitem_property_remove(mNativeData,
                                      DBUSMENU_MENUITEM_PROP_ICON_DATA);
}

void
nsMenuObject::SyncLabelFromContent()
{
    // Gecko stores the label and access key in separate attributes
    // so we need to convert label="Foo_Bar"/accesskey="F" in to
    // label="_Foo__Bar" for dbusmenu

    nsAutoString label;
    mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::label, label);

    nsAutoString accesskey;
    mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::accesskey, accesskey);

    const nsAutoString::char_type *akey = accesskey.BeginReading();
    PRUnichar keyLower = ToLowerCase(*akey);
    PRUnichar keyUpper = ToUpperCase(*akey);

    const nsAutoString::char_type *iter = label.BeginReading();
    const nsAutoString::char_type *end = label.EndReading();
    int length = label.Length();
    int pos = 0;
    bool foundAccessKey = false;

    while (iter != end) {
        if (*iter != PRUnichar('_')) {
            if ((*iter != keyLower && *iter != keyUpper) || foundAccessKey) {
                ++iter;
                ++pos;
                continue;
            }
            foundAccessKey = true;
        }

        label.SetLength(++length);

        iter = label.BeginReading() + pos;
        end = label.EndReading();
        nsAutoString::char_type *cur = label.BeginWriting() + pos;

        memmove(cur + 1, cur, (length - 1 - pos) * sizeof(nsAutoString::char_type));
        *cur = nsAutoString::char_type('_');

        iter += 2;
        pos += 2;
    }

    nsString& text = label;

    // This *COMPLETELY SUCKS*
    // This should be done at the point where the menu is drawn (hello Unity),
    // but unfortunately it doesn't do that and will happily fill your entire
    // screen width with a menu if you have a bookmark with a really long title.
    // This leaves us with no other option but to ellipsize here, with no proper
    // knowledge of Unity's render path, font size etc. This is better than nothing
    // BAH! @*&!$
    if (CalculateTextWidth(label) > MAX_WIDTH) {
        nsAutoString truncated;
        int target = MAX_WIDTH - GetEllipsisWidth();
        uint32_t length = label.Length();

        static nsIContent::AttrValuesArray strings[] = {
            &nsGkAtoms::left, &nsGkAtoms::start,
            &nsGkAtoms::center, &nsGkAtoms::right,
            &nsGkAtoms::end, nullptr
        };

        int32_t type = mContent->FindAttrValueIn(kNameSpaceID_None,
                                                 nsGkAtoms::crop,
                                                 strings, eCaseMatters);

        switch (type) {
            case 0:
            case 1:
                // FIXME: Implement left cropping (do we really care?)
            case 2:
                // FIXME: Implement center cropping (do we really care?)
            case 3:
            case 4:
            default:
                for (uint32_t i = 0; i < length; i++) {
                    truncated.Append(label.CharAt(i));
                    if (CalculateTextWidth(truncated) > target) {
                        break;
                    }
                }

                truncated.Append(GetEllipsis());
        }

        text = truncated;
    }

    dbusmenu_menuitem_property_set(mNativeData,
                                   DBUSMENU_MENUITEM_PROP_LABEL,
                                   NS_ConvertUTF16toUTF8(text).get());
}

void
nsMenuObject::SyncVisibilityFromContent()
{
    nsIPresShell *shell = mContent->OwnerDoc()->GetShell();
    if (!shell) {
        return;
    }

    nsRefPtr<nsStyleContext> sc =
        nsComputedDOMStyle::GetStyleContextForElementNoFlush(
            mContent->AsElement(), nullptr, shell);
    if (!sc) {
        return;
    }

    bool vis = true;
    if (sc->StyleDisplay()->mDisplay == NS_STYLE_DISPLAY_NONE ||
        sc->StyleVisibility()->mVisible == NS_STYLE_VISIBILITY_COLLAPSE) {
        vis = false;
    }

    dbusmenu_menuitem_property_set_bool(mNativeData,
                                        DBUSMENU_MENUITEM_PROP_VISIBLE,
                                        vis);
}

void
nsMenuObject::SyncSensitivityFromContent()
{
    bool disabled = mContent->AttrValueIs(kNameSpaceID_None,
                                          nsGkAtoms::disabled,
                                          nsGkAtoms::_true, eCaseMatters);

    dbusmenu_menuitem_property_set_bool(mNativeData,
                                        DBUSMENU_MENUITEM_PROP_ENABLED,
                                        !disabled);

}

void
nsMenuObject::SyncIconFromContent()
{
    if (ShouldShowIcon()) {
        if (!mIconLoader) {
            mIconLoader = new IconLoader(this);
        }

        mIconLoader->ScheduleIconLoad();
    } else {
        if (mIconLoader) {
            mIconLoader->Destroy();
            mIconLoader = nullptr;
        }

        ClearIcon();
    }
}

nsresult
nsMenuObject::Init(nsMenuObjectContainer *aParent, nsIContent *aContent)
{
    NS_ENSURE_ARG(aParent);
    NS_ENSURE_ARG(aContent);

    mParent = aParent;
    mContent = aContent;
    mListener = aParent->DocListener();
    NS_ENSURE_ARG(mListener);

    return NS_OK;    
}

nsMenuObject::~nsMenuObject()
{
    if (mIconLoader) {
        mIconLoader->Destroy();
    }

    if (mListener) {
        mListener->UnregisterForContentChanges(mContent);
    }

    if (mNativeData) {
        g_object_unref(mNativeData);
        mNativeData = nullptr;
    }
}

nsMenuBar*
nsMenuObject::MenuBar()
{
    nsMenuObject *tmp = static_cast<nsMenuObject *>(this);
    while (tmp->Parent()) {
        tmp = tmp->Parent();
    }

    NS_ASSERTION(tmp->Type() == eType_MenuBar, "The top-level should be a menubar");
    return static_cast<nsMenuBar *>(tmp);
}

void
nsMenuObject::CreateNativeData()
{
    NS_ASSERTION(mNativeData == nullptr, "This node already has a DbusmenuMenuitem");

    mNativeData = dbusmenu_menuitem_new();
    InitializeNativeData();
    if (mParent && mParent->IsBeingDisplayed()) {
        Update();
    }

    mListener->RegisterForContentChanges(mContent, this);
}

nsresult
nsMenuObject::AdoptNativeData(DbusmenuMenuitem *aNativeData)
{
    NS_ASSERTION(mNativeData == nullptr, "This node already has a DbusmenuMenuitem");

    if (!IsCompatibleWithNativeData(aNativeData)) {
        return NS_ERROR_FAILURE;
    }

    mNativeData = aNativeData;
    g_object_ref(mNativeData);

    RemoveUnsupportedProperties();
    InitializeNativeData();
    if (mParent && mParent->IsBeingDisplayed()) {
        Update();
    }

    mListener->RegisterForContentChanges(mContent, this);

    return NS_OK;
}

already_AddRefed<nsMenuObject>
nsMenuObjectContainer::CreateChild(nsIContent *aContent, nsresult *aRv)
{
    nsMenuObjectConstructor ctor = GetMenuObjectConstructor(aContent);
    if (!ctor) {
        // There are plenty of node types we might stumble across that
        // aren't supported. This isn't an error though
        if (aRv) {
            *aRv = NS_OK;
        }
        return nullptr;
    }

    nsRefPtr<nsMenuObject> res = ctor(this, aContent);
    if (!res) {
        if (aRv) {
            *aRv = NS_ERROR_FAILURE;
        }
        return nullptr;
    }

    if (aRv) {
        *aRv = NS_OK;
    }
    return res.forget();
}

uint32_t
nsMenuObjectContainer::IndexOf(nsIContent *aChild)
{
    if (!aChild) {
        return NoIndex;
    }

    for (uint32_t i = 0; i < mMenuObjects.Length(); ++i) {
        if (mMenuObjects[i]->ContentNode() == aChild) {
            return i;
        }
    }

    return NoIndex;
}

/* static */ nsIContent*
nsMenuObjectContainer::GetPreviousSupportedSibling(nsIContent *aContent)
{
    do {
        aContent = aContent->GetPreviousSibling();
    } while (aContent && !ContentIsSupported(aContent));

    return aContent;
}
