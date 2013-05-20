/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenuBar.h"
#include "nsIWidget.h"
#include "nsIDOMKeyEvent.h"
#include "mozilla/Preferences.h"
#include "nsIAtom.h"
#include "mozilla/dom/Element.h"
#include "nsTArray.h"
#include "nsIDOMEvent.h"
#include "nsUnicharUtils.h"
#include "mozilla/DebugOnly.h"

#include "nsNativeMenuService.h"
#include "nsMenu.h"
#include "nsNativeMenuAtoms.h"

#include <glib.h>
#include <glib-object.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

using namespace mozilla;

NS_IMPL_ISUPPORTS1(nsMenuBar::DocumentEventListener, nsIDOMEventListener)

NS_IMETHODIMP
nsMenuBar::DocumentEventListener::HandleEvent(nsIDOMEvent *aEvent)
{
    nsAutoString type;
    nsresult rv = aEvent->GetType(type);
    if (NS_FAILED(rv)) {
        NS_WARNING("Failed to determine event type");
        return rv;
    }

    if (type.Equals(NS_LITERAL_STRING("focus"))) {
        mOwner->Focus();
    } else if (type.Equals(NS_LITERAL_STRING("blur"))) {
        mOwner->Blur();
    } else if (type.Equals(NS_LITERAL_STRING("keypress"))) {
        rv = mOwner->Keypress(aEvent);
    } else if (type.Equals(NS_LITERAL_STRING("keydown"))) {
        rv = mOwner->KeyDown(aEvent);
    } else if (type.Equals(NS_LITERAL_STRING("keyup"))) {
        rv = mOwner->KeyUp(aEvent);
    }

    return rv;
}

nsresult
nsMenuBar::RemoveMenuObjectAt(uint32_t aIndex)
{
    if (aIndex >= mMenuObjects.Length()) {
        return NS_ERROR_INVALID_ARG;
    }

    if (!dbusmenu_menuitem_child_delete(
            mNativeData, mMenuObjects[aIndex]->GetNativeData())) {
        return NS_ERROR_FAILURE;
    }

    mMenuObjects.RemoveElementAt(aIndex);

    return NS_OK;
}

nsresult
nsMenuBar::RemoveMenuObject(nsIContent *aChild)
{
    uint32_t index = IndexOf(aChild);
    if (index == NoIndex) {
        return NS_ERROR_INVALID_ARG;
    }

    return RemoveMenuObjectAt(index);
}

nsresult
nsMenuBar::InsertMenuObjectAfter(nsMenuObject *aChild,
                                 nsIContent *aPrevSibling)
{
    uint32_t index = IndexOf(aPrevSibling);
    if (index == NoIndex && aPrevSibling) {
        return NS_ERROR_INVALID_ARG;
    }

    ++index;

    aChild->CreateNativeData();
    if (!dbusmenu_menuitem_child_add_position(mNativeData,
                                              aChild->GetNativeData(),
                                              index)) {
        return NS_ERROR_FAILURE;
    }

    return mMenuObjects.InsertElementAt(index, aChild) ?
        NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsMenuBar::AppendMenuObject(nsMenuObject *aChild)
{
    aChild->CreateNativeData();
    if (!dbusmenu_menuitem_child_append(mNativeData,
                                        aChild->GetNativeData())) {
        return NS_ERROR_FAILURE;
    }

    return mMenuObjects.AppendElement(aChild) ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsMenuBar::Build()
{
    uint32_t count = mContent->GetChildCount();
    for (uint32_t i = 0; i < count; ++i) {
        nsIContent *childContent = mContent->GetChildAt(i);

        nsresult rv;
        nsRefPtr<nsMenuObject> child = CreateChild(childContent, &rv);

        if (child) {
            rv = AppendMenuObject(child);
        }

        if (NS_FAILED(rv)) {
            return rv;
        }
    }

    return NS_OK;
}

void
nsMenuBar::DisconnectDocumentEventListeners()
{
    mDocument->RemoveEventListener(NS_LITERAL_STRING("focus"),
                                   mEventListener,
                                   true);
    mDocument->RemoveEventListener(NS_LITERAL_STRING("blur"),
                                   mEventListener,
                                   true);
    mDocument->RemoveEventListener(NS_LITERAL_STRING("keypress"),
                                   mEventListener,
                                   false);
    mDocument->RemoveEventListener(NS_LITERAL_STRING("keydown"),
                                   mEventListener,
                                   false);
    mDocument->RemoveEventListener(NS_LITERAL_STRING("keyup"),
                                   mEventListener,
                                   false);
}

void
nsMenuBar::SetShellShowingMenuBar(bool aShowing)
{
    mContent->OwnerDoc()->GetRootElement()->SetAttr(
        kNameSpaceID_None, nsNativeMenuAtoms::shellshowingmenubar,
        aShowing ? NS_LITERAL_STRING("true") : NS_LITERAL_STRING("false"),
        true);
}

void
nsMenuBar::Focus()
{
    mContent->SetAttr(kNameSpaceID_None, nsNativeMenuAtoms::openedwithkey,
                      NS_LITERAL_STRING("false"), true);
}

void
nsMenuBar::Blur()
{
    // We do this here in case we lose focus before getting the
    // keyup event, which leaves the menubar state looking like
    // the alt key is stuck down
    dbusmenu_server_set_status(mServer, DBUSMENU_STATUS_NORMAL);
}

static bool
ShouldHandleKeyEvent(nsIDOMEvent *aEvent)
{
    bool handled, trusted = false;
    aEvent->GetPreventDefault(&handled);
    aEvent->GetIsTrusted(&trusted);

    if (handled || !trusted) {
        return false;
    }

    return true;
}

nsMenuBar::ModifierFlags
nsMenuBar::GetModifiersFromEvent(nsIDOMKeyEvent *aEvent)
{
    ModifierFlags modifiers = ModifierFlags(0);
    bool modifier;

    aEvent->GetAltKey(&modifier);
    if (modifier) {
        modifiers = ModifierFlags(modifiers | eModifierAlt);
    }

    aEvent->GetShiftKey(&modifier);
    if (modifier) {
        modifiers = ModifierFlags(modifiers | eModifierShift);
    }

    aEvent->GetCtrlKey(&modifier);
    if (modifier) {
        modifiers = ModifierFlags(modifiers | eModifierCtrl);
    }

    aEvent->GetMetaKey(&modifier);
    if (modifier) {
        modifiers = ModifierFlags(modifiers | eModifierMeta);
    }

    return modifiers;
}

nsresult
nsMenuBar::Keypress(nsIDOMEvent *aEvent)
{
    if (!ShouldHandleKeyEvent(aEvent)) {
        return NS_OK;
    }

    nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aEvent);
    if (!keyEvent) {
        return NS_OK;
    }

    ModifierFlags modifiers = GetModifiersFromEvent(keyEvent);
    if (((modifiers & mAccessKeyMask) == 0) ||
        ((modifiers & ~mAccessKeyMask) != 0)) {
        return NS_OK;
    }

    uint32_t charCode;
    keyEvent->GetCharCode(&charCode);
    if (charCode == 0) {
        return NS_OK;
    }

    PRUnichar ch = PRUnichar(charCode);
    PRUnichar chl = ToLowerCase(ch);
    PRUnichar chu = ToUpperCase(ch);

    nsMenuObject *found = nullptr;
    uint32_t count = mMenuObjects.Length();
    for (uint32_t i = 0; i < count; ++i) {
        nsAutoString accesskey;
        mMenuObjects[i]->ContentNode()->GetAttr(kNameSpaceID_None,
                                                nsGkAtoms::accesskey,
                                                accesskey);
        const nsAutoString::char_type *key = accesskey.BeginReading();
        if (*key == chu || *key == chl) {
            found = mMenuObjects[i];
            break;
        }
    }

    if (!found) {
        return NS_OK;
    }

    NS_ASSERTION(found->Type() == nsMenuObject::eType_Menu, "Expecting a menu here");

    mContent->SetAttr(kNameSpaceID_None, nsNativeMenuAtoms::openedwithkey,
                      NS_LITERAL_STRING("true"), true);
    (static_cast<nsMenu *>(found))->OpenMenuDelayed();
    aEvent->StopPropagation();
    aEvent->PreventDefault();

    return NS_OK;
}

nsresult
nsMenuBar::KeyDown(nsIDOMEvent *aEvent)
{
    if (!ShouldHandleKeyEvent(aEvent)) {
        return NS_OK;
    }

    nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aEvent);
    if (!keyEvent) {
        return NS_OK;
    }

    uint32_t keyCode;
    keyEvent->GetKeyCode(&keyCode);
    ModifierFlags modifiers = GetModifiersFromEvent(keyEvent);
    if ((keyCode != mAccessKey) || ((modifiers & ~mAccessKeyMask) != 0)) {
        return NS_OK;
    }

    dbusmenu_server_set_status(mServer, DBUSMENU_STATUS_NOTICE);

    return NS_OK;
}

nsresult
nsMenuBar::KeyUp(nsIDOMEvent *aEvent)
{
    if (!ShouldHandleKeyEvent(aEvent)) {
        return NS_OK;
    }

    nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aEvent);
    if (!keyEvent) {
        return NS_OK;
    }

    uint32_t keyCode;
    keyEvent->GetKeyCode(&keyCode);
    if (keyCode == mAccessKey) {
        dbusmenu_server_set_status(mServer, DBUSMENU_STATUS_NORMAL);
    }

    return NS_OK;
}

nsMenuBar::nsMenuBar() :
    nsMenuObjectContainer(),
    mTopLevel(nullptr),
    mServer(nullptr),
    mFlags(0)
{
    MOZ_COUNT_CTOR(nsMenuBar);
}

nsresult
nsMenuBar::Init(nsIWidget *aParent, nsIContent *aMenuBarNode)
{
    NS_ENSURE_ARG(aParent);
    NS_ENSURE_ARG(aMenuBarNode);

    mContent = aMenuBarNode;

    GdkWindow *gdkWin = static_cast<GdkWindow *>(
        aParent->GetNativeData(NS_NATIVE_WINDOW));
    if (!gdkWin) {
        return NS_ERROR_FAILURE;
    }

    gpointer user_data = nullptr;
    gdk_window_get_user_data(gdkWin, &user_data);
    if (!user_data || !GTK_IS_CONTAINER(user_data)) {
        return NS_ERROR_FAILURE;
    }

    mTopLevel = gtk_widget_get_toplevel(GTK_WIDGET(user_data));
    if (!mTopLevel) {
        return NS_ERROR_FAILURE;
    }

    g_object_ref(mTopLevel);

    mListener = nsNativeMenuDocListener::Create(mContent);
    if (!mListener) {
        return NS_ERROR_FAILURE;
    }

    nsAutoCString path;
    path.Append(NS_LITERAL_CSTRING("/com/canonical/menu/"));
    char xid[10];
    sprintf(xid, "%X", static_cast<uint32_t>(
        GDK_WINDOW_XID(gtk_widget_get_window(mTopLevel))));
    path.Append(xid);

    mServer = dbusmenu_server_new(path.get());
    if (!mServer) {
        return NS_ERROR_FAILURE;
    }

    CreateNativeData();
    if (!mNativeData) {
        return NS_ERROR_FAILURE;
    }

    dbusmenu_server_set_root(mServer, mNativeData);

    mEventListener = new DocumentEventListener(this);

    mDocument = do_QueryInterface(mContent->OwnerDoc());

    mAccessKey = Preferences::GetInt("ui.key.menuAccessKey");
    if (mAccessKey == nsIDOMKeyEvent::DOM_VK_SHIFT) {
        mAccessKeyMask = eModifierShift;
    } else if (mAccessKey == nsIDOMKeyEvent::DOM_VK_CONTROL) {
        mAccessKeyMask = eModifierCtrl;
    } else if (mAccessKey == nsIDOMKeyEvent::DOM_VK_ALT) {
        mAccessKeyMask = eModifierAlt;
    } else if (mAccessKey == nsIDOMKeyEvent::DOM_VK_META) {
        mAccessKeyMask = eModifierMeta;
    } else {
        mAccessKeyMask = eModifierAlt;
    }

    return NS_OK;
}

nsMenuBar::~nsMenuBar()
{
    nsNativeMenuService *service = nsNativeMenuService::GetSingleton();
    if (service) {
        service->NotifyNativeMenuBarDestroyed(this);
    }

    if (mContent) {
        SetShellShowingMenuBar(false);
    }

    if (mTopLevel) {
        g_object_unref(mTopLevel);
    }

    if (mListener) {
        mListener->Stop();
    }

    if (mDocument) {
        DisconnectDocumentEventListeners();
    }

    if (mServer) {
        g_object_unref(mServer);
    }

    MOZ_COUNT_DTOR(nsMenuBar);
}

/* static */ already_AddRefed<nsMenuBar>
nsMenuBar::Create(nsIWidget *aParent, nsIContent *aMenuBarNode)
{
    nsRefPtr<nsMenuBar> menubar = new nsMenuBar();
    if (NS_FAILED(menubar->Init(aParent, aMenuBarNode))) {
        return nullptr;
    }

    return menubar.forget();
}

uint32_t
nsMenuBar::WindowId() const
{
    return static_cast<uint32_t>(GDK_WINDOW_XID(gtk_widget_get_window(mTopLevel)));
}

nsAdoptingCString
nsMenuBar::ObjectPath() const
{
    gchar *tmp;
    g_object_get(mServer, DBUSMENU_SERVER_PROP_DBUS_OBJECT, &tmp, NULL);
    nsAdoptingCString result(tmp);

    return result;
}

nsresult
nsMenuBar::Activate()
{
    if (IsActive()) {
        return NS_OK;
    }

    SetIsActiveFlag();

    mDocument->AddEventListener(NS_LITERAL_STRING("focus"),
                                mEventListener,
                                true);
    mDocument->AddEventListener(NS_LITERAL_STRING("blur"),
                                mEventListener,
                                true);
    mDocument->AddEventListener(NS_LITERAL_STRING("keypress"),
                                mEventListener,
                                false);
    mDocument->AddEventListener(NS_LITERAL_STRING("keydown"),
                                mEventListener,
                                false);
    mDocument->AddEventListener(NS_LITERAL_STRING("keyup"),
                                mEventListener,
                                false);

    // Clear this. Not sure if we really need to though
    mContent->SetAttr(kNameSpaceID_None, nsNativeMenuAtoms::openedwithkey,
                      NS_LITERAL_STRING("false"), true);

    mListener->Start();

    nsresult rv = Build();
    if (NS_FAILED(rv)) {
        Deactivate();
        return rv;
    }

    SetShellShowingMenuBar(true);

    return NS_OK;
}

void
nsMenuBar::Deactivate()
{
    if (!IsActive()) {
        return;
    }

    ClearIsActiveFlag();

    mRegisterRequestCanceller.Cancel();

    SetShellShowingMenuBar(false);
    while (mMenuObjects.Length() > 0) {
        RemoveMenuObjectAt(0);
    }
    mListener->Stop();
    DisconnectDocumentEventListeners();
}

void
nsMenuBar::OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute)
{

}

void
nsMenuBar::OnContentInserted(nsIContent *aContainer, nsIContent *aChild,
                             nsIContent *aPrevSibling)
{
    NS_ASSERTION(aContainer == mContent,
                 "Received an event that wasn't meant for us");

    nsresult rv;
    nsRefPtr<nsMenuObject> child = CreateChild(aChild, &rv);

    if (child) {
        rv = InsertMenuObjectAfter(child, aPrevSibling);
    }

    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to insert item in to menubar");
}

void
nsMenuBar::OnContentRemoved(nsIContent *aContainer, nsIContent *aChild)
{
    NS_ASSERTION(aContainer == mContent,
                 "Received an event that wasn't meant for us");

    DebugOnly<nsresult> rv = RemoveMenuObject(aChild);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to remove item from menubar");
}
