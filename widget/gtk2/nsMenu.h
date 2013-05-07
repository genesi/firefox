/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsMenu_h__
#define __nsMenu_h__

#include "nsCOMPtr.h"
#include "nsIContent.h"
#include "mozilla/Attributes.h"

#include "nsMenuObject.h"
#include "nsDbusmenu.h"

#include <glib.h>

class nsIAtom;

#define NSMENU_NUMBER_OF_POPUPSTATE_BITS 3U
#define NSMENU_NUMBER_OF_FLAGS           4U

// This class represents a menu
class nsMenu MOZ_FINAL : public nsMenuObjectContainer
{
public:
    ~nsMenu();

    static already_AddRefed<nsMenuObject> Create(nsMenuObject *aParent,
                                                 nsIContent *aContent);

    nsMenuObject::EType Type() const { return nsMenuObject::eType_Menu; }

    // Tell the desktop shell to display this menu
    void OpenMenuDelayed();

    void OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute);
    void OnContentInserted(nsIContent *aContainer, nsIContent *aChild,
                           nsIContent *aPrevSibling);
    void OnContentRemoved(nsIContent *aContainer, nsIContent *aChild);
    void BeginUpdateBatch(nsIContent *aContent);
    void EndUpdateBatch();

private:
    enum {
        eFlag_NeedsRebuild = 1 << 0,
        eFlag_IgnoreFirstAboutToShow = 1 << 1,
        eFlag_InUpdateBatch = 1 << 2,
        eFlag_StructureMutated = 1 << 3
    };

    enum EAboutToOpenOrigin {
        eAboutToOpenOrigin_FromAboutToShowSignal,
        eAboutToOpenOrigin_FromOpenedEvent
    };

    enum EPopupState {
        ePopupState_Closed,
        ePopupState_Showing,
        ePopupState_OpenFromAboutToShow,
        ePopupState_OpenFromOpenedEvent,
        ePopupState_Hiding
    };

    nsMenu();
    nsresult ImplInit();

    bool NeedsRebuild() const {
        return mFlags & eFlag_NeedsRebuild ? true : false;
    }
    void SetNeedsRebuildFlag() {
        mFlags |= eFlag_NeedsRebuild;
    }
    void ClearNeedsRebuildFlag() {
        mFlags &= ~eFlag_NeedsRebuild;
    }

    bool IgnoreFirstAboutToShow() const {
        return mFlags & eFlag_IgnoreFirstAboutToShow ? true : false;
    }
    void SetIgnoreFirstAboutToShowFlag() {
        mFlags |= eFlag_IgnoreFirstAboutToShow;
    }
    void ClearIgnoreFirstAboutToShowFlag() {
        mFlags &= ~eFlag_IgnoreFirstAboutToShow;
    }

    bool IsInUpdateBatch() const {
        return mFlags & eFlag_InUpdateBatch ? true : false;
    }
    void SetIsInUpdateBatchFlag() {
        mFlags |= eFlag_InUpdateBatch;
    }
    void ClearIsInUpdateBatchFlag() {
        mFlags &= ~eFlag_InUpdateBatch;
    }

    bool DidStructureMutate() const {
        return mFlags & eFlag_StructureMutated;
    }
    void SetStructureMutatedFlag() {
        mFlags |= eFlag_StructureMutated;
    }
    void ClearStructureMutatedFlag() {
        mFlags &= ~eFlag_StructureMutated;
    }

    EPopupState PopupState() const {
        return EPopupState(
            (mFlags &
             (((1U << NSMENU_NUMBER_OF_POPUPSTATE_BITS) - 1U)
              << NSMENU_NUMBER_OF_FLAGS)) >> NSMENU_NUMBER_OF_FLAGS);
    };
    void SetPopupState(EPopupState aState);

    static gboolean menu_about_to_show_cb(DbusmenuMenuitem *menu,
                                          gpointer user_data);
    static void menu_event_cb(DbusmenuMenuitem *menu,
                              const gchar *name,
                              GVariant *value,
                              guint timestamp,
                              gpointer user_data);
    void AboutToOpen(EAboutToOpenOrigin aOrigin);
    void OnClose();
    nsresult Build();
    void InitializeNativeData();
    void Refresh(nsMenuObject::ERefreshType aType);
    void InitializePopup();
    nsresult RemoveMenuObjectAt(uint32_t aIndex);
    nsresult RemoveMenuObject(nsIContent *aChild);
    nsresult InsertMenuObjectAfter(nsMenuObject *aChild, nsIContent *aPrevSibling);
    nsresult AppendMenuObject(nsMenuObject *aChild);
    bool CanOpen() const;

    nsMenuObject::PropertyFlags SupportedProperties() const {
        return nsMenuObject::PropertyFlags(
            nsMenuObject::ePropLabel |
            nsMenuObject::ePropEnabled |
            nsMenuObject::ePropVisible |
            nsMenuObject::ePropIconData |
            nsMenuObject::ePropChildDisplay
        );
    }

    nsCOMPtr<nsIContent> mPopupContent;
    uint8_t mFlags; // The upper bits of this are used to encode the popup state
};

#endif /* __nsMenu_h__ */
