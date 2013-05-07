/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsMenuItem_h__
#define __nsMenuItem_h__

#include "nsIContent.h"
#include "nsCOMPtr.h"
#include "mozilla/Attributes.h"

#include "nsMenuObject.h"
#include "nsDbusmenu.h"

#include <glib.h>

#define NSMENUITEM_NUMBER_OF_TYPE_BITS 2U
#define NSMENUITEM_NUMBER_OF_FLAGS     1U

class nsIAtom;

/*
 * This class represents 3 main classes of menuitems: labels, checkboxes and
 * radio buttons (with/without an icon)
 */
class nsMenuItem MOZ_FINAL : public nsMenuObject
{
public:
    ~nsMenuItem();

    nsMenuObject::EType Type() const { return nsMenuObject::eType_MenuItem; }

    static already_AddRefed<nsMenuObject> Create(nsMenuObject *aParent,
                                                 nsIContent *aContent);

    void OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute);

private:
    friend class nsMenuItemRefreshRunnable;
    friend class nsMenuItemCommandNodeAttributeChangedRunnable;
    friend class nsMenuItemUncheckSiblingsRunnable;

    enum {
        eMenuItemFlag_ToggleState = (1 << 0)
    };

    enum EMenuItemType {
        eMenuItemType_Normal,
        eMenuItemType_Radio,
        eMenuItemType_CheckBox
    };

    nsMenuItem();

    EMenuItemType MenuItemType() const {
        return EMenuItemType(
            (mFlags &
             (((1U << NSMENUITEM_NUMBER_OF_TYPE_BITS) - 1U)
              << NSMENUITEM_NUMBER_OF_TYPE_BITS)) >> NSMENUITEM_NUMBER_OF_TYPE_BITS);
    }
    void SetMenuItemType(EMenuItemType aType) {
        mFlags &= ~(((1U << NSMENUITEM_NUMBER_OF_TYPE_BITS) - 1U) << NSMENUITEM_NUMBER_OF_TYPE_BITS);
        mFlags |= (aType << NSMENUITEM_NUMBER_OF_TYPE_BITS);
    }
    bool IsCheckboxOrRadioItem() const {
        return MenuItemType() == eMenuItemType_Radio ||
               MenuItemType() == eMenuItemType_CheckBox;
    }

    bool IsChecked() const {
        return mFlags & eMenuItemFlag_ToggleState ? true : false;
    }
    void SetCheckState(bool aState) {
        if (aState) {
            mFlags |= eMenuItemFlag_ToggleState;
        } else {
            mFlags &= ~eMenuItemFlag_ToggleState;
        }
    }

    static void item_activated_cb(DbusmenuMenuitem *menuitem,
                                  guint timestamp,
                                  gpointer user_data);
    void Activate(uint32_t aTimestamp);

    void SyncStateFromCommand();
    void SyncLabelFromCommand();
    void SyncSensitivityFromCommand();
    void SyncStateFromContent();
    void SyncTypeAndStateFromContent();
    void SyncAccelFromContent();

    void CommandNodeAttributeChanged(nsIAtom *aAttribute);

    void InitializeNativeData();
    void Refresh(nsMenuObject::ERefreshType aType);
    void RefreshUnblocked(nsMenuObject::ERefreshType aType);
    void UncheckSiblings();
    bool IsCompatibleWithNativeData(DbusmenuMenuitem *aNativeData);

    nsMenuObject::PropertyFlags SupportedProperties() const {
        return nsMenuObject::PropertyFlags(
            nsMenuObject::ePropLabel |
            nsMenuObject::ePropEnabled |
            nsMenuObject::ePropVisible |
            nsMenuObject::ePropIconData |
            nsMenuObject::ePropShortcut |
            nsMenuObject::ePropToggleType |
            nsMenuObject::ePropToggleState
        );
    }

    nsCOMPtr<nsIContent> mKeyContent;
    nsCOMPtr<nsIContent> mCommandContent;

    uint8_t mFlags; // The upper bits of this are used to encode the menuitem type
};

#endif /* __nsMenuItem_h__ */
