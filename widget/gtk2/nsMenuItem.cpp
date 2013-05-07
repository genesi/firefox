/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenuItem.h"
#include "nsIDocument.h"
#include "mozilla/dom/Element.h"
#include "nsString.h"
#include "nsIAtom.h"
#include "nsGkAtoms.h"
#include "mozilla/Preferences.h"
#include "nsIDOMKeyEvent.h"
#include "nsReadableUtils.h"
#include "mozilla/Util.h"
#include "nsGtkKeyUtils.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMDocument.h"
#include "nsIDOMXULCommandEvent.h"
#include "nsIDOMEvent.h"
#include "nsIRunnable.h"
#include "nsContentUtils.h"
#include "nsThreadUtils.h"
#include "nsCRT.h"

#include "nsNativeMenuDocListener.h"
#include "nsNativeMenuUtils.h"
#include "nsMenuBar.h"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

using namespace mozilla;
using namespace mozilla::widget;

struct keyCodeData {
    const char* str;
    size_t strlength;
    uint32_t keycode;
};

static struct keyCodeData gKeyCodes[] = {
#define NS_DEFINE_VK(aDOMKeyName, aDOMKeyCode) \
  { #aDOMKeyName, sizeof(#aDOMKeyName) - 1, aDOMKeyCode }
#include "nsVKList.h"
#undef NS_DEFINE_VK
};

static uint32_t
GetKeyCode(nsAString& aKeyName)
{
    NS_ConvertUTF16toUTF8 keyName(aKeyName);
    ToUpperCase(keyName); // We want case-insensitive comparison with data
                          // stored as uppercase.

    uint32_t keyNameLength = keyName.Length();
    const char* keyNameStr = keyName.get();
    for (uint16_t i = 0; i < ArrayLength(gKeyCodes); ++i) {
        if (keyNameLength == gKeyCodes[i].strlength &&
            !nsCRT::strcmp(gKeyCodes[i].str, keyNameStr)) {
            return gKeyCodes[i].keycode;
        }
    }

    return 0;
}

class nsMenuItemUncheckSiblingsRunnable MOZ_FINAL : public nsRunnable {
public:
    NS_IMETHODIMP Run() {
        mMenuItem->UncheckSiblings();
        return NS_OK;
    }

    nsMenuItemUncheckSiblingsRunnable(nsMenuItem *aMenuItem) :
        mMenuItem(aMenuItem) { };

private:
    nsRefPtr<nsMenuItem> mMenuItem;
};

class nsMenuItemRefreshRunnable MOZ_FINAL : public nsRunnable {
public:
    NS_IMETHODIMP Run() {
        mMenuItem->RefreshUnblocked(mType);
        return NS_OK;
    }

    nsMenuItemRefreshRunnable(nsMenuItem *aMenuItem,
                              nsMenuObject::ERefreshType aType) :
        mMenuItem(aMenuItem),
        mType(aType) { };

private:
    nsRefPtr<nsMenuItem> mMenuItem;
    nsMenuObject::ERefreshType mType;
};

class nsMenuItemCommandNodeAttributeChangedRunnable MOZ_FINAL :
    public nsRunnable
{
public:
    NS_IMETHODIMP Run() {
        mMenuItem->CommandNodeAttributeChanged(mAttribute);
        return NS_OK;
    }

    nsMenuItemCommandNodeAttributeChangedRunnable(nsMenuItem *aMenuItem,
                                                  nsIAtom *aAttribute) :
        mMenuItem(aMenuItem),
        mAttribute(aAttribute) { };

private:
    nsRefPtr<nsMenuItem> mMenuItem;
    nsCOMPtr<nsIAtom> mAttribute;
};

/* static */ void
nsMenuItem::item_activated_cb(DbusmenuMenuitem *menuitem,
                              guint timestamp,
                              gpointer user_data)
{
    nsMenuItem *item = static_cast<nsMenuItem *>(user_data);
    item->Activate(timestamp);
}

void
nsMenuItem::Activate(uint32_t aTimestamp)
{
    gdk_x11_window_set_user_time(
        gtk_widget_get_window(MenuBar()->TopLevelWindow()),
        aTimestamp);

    nsNativeMenuAutoSuspendMutations as;

    // This first bit seems backwards, but it's not really. If autocheck is
    // not set or autocheck==true, then the checkbox state is usually updated
    // by the input event that clicked on the menu item. In this case, we need
    // to manually update the checkbox state. If autocheck==false, then the input 
    // event doesn't toggle the checkbox state, and it is up  to the handler on
    // this node to do it instead. In this case, we leave it alone

    // XXX: it is important to update the checkbox state before dispatching
    //      the event, as the handler might check the new state
    if (!mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::autocheck,
                               nsGkAtoms::_false, eCaseMatters) &&
        (MenuItemType() == eMenuItemType_CheckBox ||
         (MenuItemType() == eMenuItemType_Radio && !IsChecked()))) {
        mContent->SetAttr(kNameSpaceID_None, nsGkAtoms::checked,
                          IsChecked() ?
                          NS_LITERAL_STRING("false") :  NS_LITERAL_STRING("true"),
                          true);
    }

    nsIDocument *doc = mContent->OwnerDoc();
    nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(mContent);
    nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(doc);
    if (!domDoc || !target) {
        return;
    }

    nsCOMPtr<nsIDOMEvent> event;
    domDoc->CreateEvent(NS_LITERAL_STRING("xulcommandevent"),
                        getter_AddRefs(event));
    nsCOMPtr<nsIDOMXULCommandEvent> command = do_QueryInterface(event);
    if (!command) {
        return;
    }

    command->InitCommandEvent(NS_LITERAL_STRING("command"),
                              true, true, doc->GetWindow(), 0,
                              false, false, false, false, nullptr);

    event->SetTrusted(true);
    bool dummy;
    target->DispatchEvent(event, &dummy);
}

void
nsMenuItem::SyncStateFromCommand()
{
    nsAutoString checked;
    if (mCommandContent && mCommandContent->GetAttr(kNameSpaceID_None,
                                                    nsGkAtoms::checked,
                                                    checked)) {
        mContent->SetAttr(kNameSpaceID_None, nsGkAtoms::checked, checked,
                          true);
    }
}

void
nsMenuItem::SyncLabelFromCommand()
{
    nsAutoString label;
    if (mCommandContent && mCommandContent->GetAttr(kNameSpaceID_None,
                                                    nsGkAtoms::label,
                                                    label)) {
        mContent->SetAttr(kNameSpaceID_None, nsGkAtoms::label, label,
                          true);
    }
}

void
nsMenuItem::SyncSensitivityFromCommand()
{
    if (mCommandContent) {
        if (mCommandContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::disabled,
                                         nsGkAtoms::_true, eCaseMatters)) {
            mContent->SetAttr(kNameSpaceID_None, nsGkAtoms::disabled,
                              NS_LITERAL_STRING("true"), true);
        } else {
            mContent->UnsetAttr(kNameSpaceID_None, nsGkAtoms::disabled, true);
        }
    }
}

void
nsMenuItem::SyncStateFromContent()
{
    if (!IsCheckboxOrRadioItem()) {
        return;
    }

    SetCheckState(mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::checked,
                                        nsGkAtoms::_true, eCaseMatters));
    dbusmenu_menuitem_property_set_int(mNativeData,
                                       DBUSMENU_MENUITEM_PROP_TOGGLE_STATE,
                                       IsChecked() ?
                                         DBUSMENU_MENUITEM_TOGGLE_STATE_CHECKED : 
                                         DBUSMENU_MENUITEM_TOGGLE_STATE_UNCHECKED);
}

void
nsMenuItem::SyncTypeAndStateFromContent()
{
    static nsIContent::AttrValuesArray attrs[] =
        { &nsGkAtoms::checkbox, &nsGkAtoms::radio, nullptr };
    int32_t type = mContent->FindAttrValueIn(kNameSpaceID_None,
                                             nsGkAtoms::type,
                                             attrs, eCaseMatters);

    if (type >= 0 && type < 2) {
        if (type == 0) {
            dbusmenu_menuitem_property_set(mNativeData,
                                           DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
                                           DBUSMENU_MENUITEM_TOGGLE_CHECK);
            SetMenuItemType(eMenuItemType_CheckBox);
        } else if (type == 1) {
            dbusmenu_menuitem_property_set(mNativeData,
                                           DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
                                           DBUSMENU_MENUITEM_TOGGLE_RADIO);
            SetMenuItemType(eMenuItemType_Radio);
        }

        SyncStateFromContent();
    } else {
        dbusmenu_menuitem_property_remove(mNativeData,
                                          DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE);
        dbusmenu_menuitem_property_remove(mNativeData,
                                          DBUSMENU_MENUITEM_PROP_TOGGLE_STATE);
        SetMenuItemType(eMenuItemType_Normal);
    }
}

void
nsMenuItem::SyncAccelFromContent()
{
    if (!mKeyContent) {
        dbusmenu_menuitem_property_remove(mNativeData,
                                          DBUSMENU_MENUITEM_PROP_SHORTCUT);
        return;
    }

    nsAutoString modifiers;
    mKeyContent->GetAttr(kNameSpaceID_None, nsGkAtoms::modifiers, modifiers);

    uint32_t modifier = 0;

    if (!modifiers.IsEmpty()) {
        char* str = ToNewUTF8String(modifiers);
        char *token = strtok(str, ", \t");
        while(token) {
            if (nsCRT::strcmp(token, "shift") == 0) {
                modifier |= GDK_SHIFT_MASK;
            } else if (nsCRT::strcmp(token, "alt") == 0) {
                modifier |= GDK_MOD1_MASK;
            } else if (nsCRT::strcmp(token, "meta") == 0) {
                modifier |= GDK_META_MASK;
            } else if (nsCRT::strcmp(token, "control") == 0) {
                modifier |= GDK_CONTROL_MASK;
            } else if (nsCRT::strcmp(token, "accel") == 0) {
                int32_t accel = Preferences::GetInt("ui.key.accelKey");
                if (accel == nsIDOMKeyEvent::DOM_VK_META) {
                    modifier |= GDK_META_MASK;
                } else if (accel == nsIDOMKeyEvent::DOM_VK_ALT) {
                    modifier |= GDK_MOD1_MASK;
                } else {
                    modifier |= GDK_CONTROL_MASK;
                }
            }

            token = strtok(nullptr, ", \t");
        }

        nsMemory::Free(str);
    }

    nsAutoString keyStr;
    mKeyContent->GetAttr(kNameSpaceID_None, nsGkAtoms::key, keyStr);

    guint key = 0;
    if (!keyStr.IsEmpty()) {
        key = gdk_unicode_to_keyval(*keyStr.BeginReading());
    }

    if (key == 0) {
        mKeyContent->GetAttr(kNameSpaceID_None, nsGkAtoms::keycode, keyStr);
        if (!keyStr.IsEmpty())
            key = KeymapWrapper::GuessGDKKeyval(GetKeyCode(keyStr));
    }

    if (key == 0) {
        key = GDK_VoidSymbol;
    }

    if (key != GDK_VoidSymbol) {
        dbusmenu_menuitem_property_set_shortcut(mNativeData, key,
                                                static_cast<GdkModifierType>(modifier));
    } else {
        dbusmenu_menuitem_property_remove(mNativeData,
                                          DBUSMENU_MENUITEM_PROP_SHORTCUT);
    }
}

void
nsMenuItem::CommandNodeAttributeChanged(nsIAtom *aAttribute)
{
    if (aAttribute == nsGkAtoms::label) {
        SyncLabelFromCommand();
    } else if (aAttribute == nsGkAtoms::disabled) {
        SyncSensitivityFromCommand();
    } else if (aAttribute == nsGkAtoms::checked) {
        SyncStateFromCommand();
    }
}

void
nsMenuItem::InitializeNativeData()
{
    g_signal_connect(G_OBJECT(mNativeData),
                     DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
                     G_CALLBACK(item_activated_cb), this);
}

void
nsMenuItem::Refresh(nsMenuObject::ERefreshType aType)
{
    if (aType == eRefreshType_Full) {
        if (mCommandContent) {
            mListener->UnregisterForContentChanges(mCommandContent, this);
            mCommandContent = nullptr;
        }
        if (mKeyContent) {
            mListener->UnregisterForContentChanges(mKeyContent, this);
            mKeyContent = nullptr;
        }

        nsIDocument *doc = mContent->GetCurrentDoc();
        nsAutoString command;
        mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::command, command);
        if (!command.IsEmpty()) {
            mCommandContent = doc->GetElementById(command);
            if (mCommandContent) {
                mListener->RegisterForContentChanges(mCommandContent, this);
            }
        }

        nsAutoString key;
        mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::key, key);
        if (!key.IsEmpty()) {
            mKeyContent = doc->GetElementById(key);
            if (mKeyContent) {
                mListener->RegisterForContentChanges(mKeyContent, this);
            }
        }
    }

    if (nsContentUtils::IsSafeToRunScript()) {
        RefreshUnblocked(aType);
    } else {
        nsContentUtils::AddScriptRunner(new nsMenuItemRefreshRunnable(this,
                                                                      aType));
    }
}

void
nsMenuItem::RefreshUnblocked(nsMenuObject::ERefreshType aType)
{
    SyncStateFromCommand();
    SyncLabelFromCommand();
    SyncSensitivityFromCommand();

    if (aType == eRefreshType_Full) {
        SyncTypeAndStateFromContent();
        SyncAccelFromContent();
        SyncLabelFromContent();
        SyncSensitivityFromContent();
    }

    SyncVisibilityFromContent();
    SyncIconFromContent();
}

void
nsMenuItem::UncheckSiblings()
{
    if (!mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                               nsGkAtoms::radio, eCaseMatters)) {
        // If we're not a radio button, we don't care
        return;
    }

    nsAutoString name;
    mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::name, name);

    nsIContent *parent = mContent->GetParent();
    if (!parent) {
        return;
    }

    uint32_t count = parent->GetChildCount();
    for (uint32_t i = 0; i < count; ++i) {
        nsIContent *sibling = parent->GetChildAt(i);

        nsAutoString otherName;
        sibling->GetAttr(kNameSpaceID_None, nsGkAtoms::name, otherName);

        if (sibling != mContent && otherName == name && 
            sibling->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                                 nsGkAtoms::radio, eCaseMatters)) {
            sibling->UnsetAttr(kNameSpaceID_None, nsGkAtoms::checked, true);
        }
    }
}

bool
nsMenuItem::IsCompatibleWithNativeData(DbusmenuMenuitem *aNativeData)
{
    return nsCRT::strcmp(dbusmenu_menuitem_property_get(aNativeData,
                                                        DBUSMENU_MENUITEM_PROP_TYPE),
                         "separator") != 0;
}

nsMenuItem::nsMenuItem() :
    nsMenuObject(),
    mFlags(0)
{
    MOZ_COUNT_CTOR(nsMenuItem);
}

nsMenuItem::~nsMenuItem()
{
    if (mListener) {
        if (mCommandContent) {
            mListener->UnregisterForContentChanges(mCommandContent, this);
        }
        if (mKeyContent) {
            mListener->UnregisterForContentChanges(mKeyContent, this);
        }
    }

    if (mNativeData) {
        g_signal_handlers_disconnect_by_func(mNativeData,
                                             nsNativeMenuUtils::FuncToVoidPtr(item_activated_cb),
                                             this);
    }

    MOZ_COUNT_DTOR(nsMenuItem);
}

/* static */ already_AddRefed<nsMenuObject>
nsMenuItem::Create(nsMenuObject *aParent,
                   nsIContent *aContent)
{
    nsRefPtr<nsMenuItem> menuitem = new nsMenuItem();
    if (NS_FAILED(menuitem->Init(aParent, aContent))) {
        return nullptr;
    }

    return menuitem.forget();
}

void
nsMenuItem::OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute)
{
    NS_ASSERTION(aContent == mContent || aContent == mCommandContent ||
                 aContent == mKeyContent,
                 "Received an event that wasn't meant for us!");

    if (aContent == mContent && aAttribute == nsGkAtoms::checked &&
        aContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::checked,
                              nsGkAtoms::_true, eCaseMatters)) {
        if (nsContentUtils::IsSafeToRunScript()) {
            UncheckSiblings();
        } else {
            nsContentUtils::AddScriptRunner(
                new nsMenuItemUncheckSiblingsRunnable(this));
        }
    }

    if (aContent == mContent) {
        if (aAttribute == nsGkAtoms::command ||
            aAttribute == nsGkAtoms::key) {
            Refresh(eRefreshType_Full);
        } else if (aAttribute == nsGkAtoms::label ||
                   aAttribute == nsGkAtoms::accesskey ||
                   aAttribute == nsGkAtoms::crop) {
            SyncLabelFromContent();
        } else if (aAttribute == nsGkAtoms::disabled) {
            SyncSensitivityFromContent();
        } else if (aAttribute == nsGkAtoms::type) {
            SyncTypeAndStateFromContent();
        } else if (aAttribute == nsGkAtoms::checked) {
            SyncStateFromContent();
        } else if (aAttribute == nsGkAtoms::image) {
            SyncIconFromContent();
        } else if (aAttribute == nsGkAtoms::hidden ||
                   aAttribute == nsGkAtoms::collapsed) {
            SyncVisibilityFromContent();
        }
    } else if (aContent == mCommandContent) {
        if (nsContentUtils::IsSafeToRunScript()) {
            CommandNodeAttributeChanged(aAttribute);
        } else {
            nsContentUtils::AddScriptRunner(
                new nsMenuItemCommandNodeAttributeChangedRunnable(this,
                                                                  aAttribute));
        }
    } else if (aContent == mKeyContent) {
        SyncAccelFromContent();
    }
}
