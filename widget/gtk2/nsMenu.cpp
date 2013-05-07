/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenu.h"
#include "nsString.h"
#include "nsGUIEvent.h"
#include "nsBindingManager.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptContext.h"
#include "jsapi.h"
#include "mozilla/Services.h"
#include "nsIAtom.h"
#include "nsIDocument.h"
#include "nsGkAtoms.h"
#include "nsITimer.h"
#include "nsComponentManagerUtils.h"
#include "nsIRunnable.h"
#include "nsContentUtils.h"
#include "nsLayoutUtils.h"

#include "nsNativeMenuDocListener.h"
#include "nsNativeMenuUtils.h"
#include "nsMenuBar.h"
#include "nsNativeMenuAtoms.h"

#include <glib-object.h>

using namespace mozilla;
using mozilla::AutoPushJSContext;

void
nsMenu::SetPopupState(EPopupState aState)
{
    mFlags &= ~(((1U << NSMENU_NUMBER_OF_POPUPSTATE_BITS) - 1U) << NSMENU_NUMBER_OF_FLAGS);
    mFlags |= (aState << NSMENU_NUMBER_OF_FLAGS);

    if (!mPopupContent) {
        return;
    }

    nsAutoString state;
    switch (aState) {
        case ePopupState_Showing:
            state.Assign(NS_LITERAL_STRING("showing"));
            break;
        case ePopupState_OpenFromAboutToShow:
        case ePopupState_OpenFromOpenedEvent:
            state.Assign(NS_LITERAL_STRING("open"));
            break;
        case ePopupState_Hiding:
            state.Assign(NS_LITERAL_STRING("hiding"));
            break;
        default:
            break;
    }

    nsCOMPtr<nsIRunnable> event;
    if (nsContentUtils::IsSafeToRunScript()) {
        if (state.IsEmpty()) {
            mPopupContent->UnsetAttr(kNameSpaceID_None,
                                     nsNativeMenuAtoms::_moz_menupopupstate,
                                     true);
        } else {
            mPopupContent->SetAttr(kNameSpaceID_None,
                                   nsNativeMenuAtoms::_moz_menupopupstate,
                                   state, true);
        }
    } else {
        if (state.IsEmpty()) {
            event = new nsUnsetAttrRunnable(
                mPopupContent, nsNativeMenuAtoms::_moz_menupopupstate);
        } else {
            event =
                new nsSetAttrRunnable(mPopupContent,
                                      nsNativeMenuAtoms::_moz_menupopupstate,
                                      state);
        }
        nsContentUtils::AddScriptRunner(event);
    }
}

/* static */ gboolean
nsMenu::menu_about_to_show_cb(DbusmenuMenuitem *menu,
                              gpointer user_data)
{
    nsMenu *self = static_cast<nsMenu *>(user_data);

    if (self->IgnoreFirstAboutToShow()) {
        self->ClearIgnoreFirstAboutToShowFlag();
        return FALSE;
    }

    self->AboutToOpen(eAboutToOpenOrigin_FromAboutToShowSignal);

    return FALSE;
}

/* static */ void
nsMenu::menu_event_cb(DbusmenuMenuitem *menu,
                      const gchar *name,
                      GVariant *value,
                      guint timestamp,
                      gpointer user_data)
{
    nsMenu *self = static_cast<nsMenu *>(user_data);

    nsAutoCString event(name);

    if (event.Equals(NS_LITERAL_CSTRING("closed"))) {
        self->OnClose();
        return;
    }

    if (event.Equals(NS_LITERAL_CSTRING("opened"))) {
        self->AboutToOpen(eAboutToOpenOrigin_FromOpenedEvent);
        return;
    }
}

static void
DispatchMouseEvent(nsIContent *aTarget, uint32_t aMsg)
{
    if (!aTarget) {
        return;
    }

    nsMouseEvent event(true, aMsg, nullptr, nsMouseEvent::eReal);
    aTarget->DispatchDOMEvent(&event, nullptr, nullptr, nullptr);
}

void
nsMenu::AboutToOpen(EAboutToOpenOrigin aOrigin)
{
    // This function can be called in 2 ways:
    // - "about-to-show" signal from dbusmenu
    // - "opened" event from dbusmenu
    // Unity sends both of these, but we only want to respond to one of them.
    // Unity-2D only sends the second one.
    // To complicate things even more, Unity doesn't send us a closed event
    // when a menuitem is activated, so we need to accept consecutive
    // about-to-show signals without corresponding "closed" events (else we would
    // just ignore all calls to this function once the menu is open). So, the
    // way this works is:
    // - If we were called from an "about-to-show" signal, ignore the "opened"
    //   event and accept consecutive "about-to-show" signals
    // - If we were called from an "opened" event, accept any consecutve call
    //   without a corresponding "closed" event

    if (PopupState() == ePopupState_Showing ||
        (PopupState() == ePopupState_OpenFromAboutToShow &&
         aOrigin == eAboutToOpenOrigin_FromOpenedEvent)) {
        return;
    }

    nsNativeMenuAutoSuspendMutations as;

    if (NeedsRebuild()) {
        if (NS_FAILED(Build())) {
            NS_WARNING("Menu build failed - marking invalid");
            SetNeedsRebuildFlag();
            return;
        }
    }

    SetPopupState(ePopupState_Showing);
    DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_SHOWING);

    uint32_t count = mMenuObjects.Length();
    for (uint32_t i = 0; i < count; ++i) {
        mMenuObjects[i]->ContainerIsOpening();
    }

    EPopupState openState = aOrigin == eAboutToOpenOrigin_FromOpenedEvent ?
        ePopupState_OpenFromOpenedEvent : ePopupState_OpenFromAboutToShow;
    SetPopupState(openState);
    mContent->SetAttr(kNameSpaceID_None, nsGkAtoms::open,
                      NS_LITERAL_STRING("true"), true);
    DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_SHOWN);
}

void
nsMenu::OnClose()
{
    if (PopupState() != ePopupState_Showing &&
        PopupState() != ePopupState_OpenFromAboutToShow &&
        PopupState() != ePopupState_OpenFromOpenedEvent) {
        return;
    }

    nsNativeMenuAutoSuspendMutations as;

    SetPopupState(ePopupState_Hiding);

    DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_HIDING);
    SetPopupState(ePopupState_Closed);

    DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_HIDDEN);

    mContent->UnsetAttr(kNameSpaceID_None, nsGkAtoms::open, true);
}

nsresult
nsMenu::Build()
{
    while (mMenuObjects.Length() > 0) {
        RemoveMenuObjectAt(0);
    }

    InitializePopup();

    if (!mPopupContent) {
        return NS_OK;
    }

    ClearNeedsRebuildFlag();

    uint32_t count = mPopupContent->GetChildCount();
    for (uint32_t i = 0; i < count; ++i) {
        nsIContent *childContent = mPopupContent->GetChildAt(i);

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
nsMenu::InitializeNativeData()
{
    // This happens automatically when we add children, but we have to
    // do this manually for menus which don't initially have children,
    // so we can receive about-to-show which triggers a build of the menu
    dbusmenu_menuitem_property_set(mNativeData,
                                   DBUSMENU_MENUITEM_PROP_CHILD_DISPLAY,
                                   DBUSMENU_MENUITEM_CHILD_DISPLAY_SUBMENU);

    g_signal_connect(G_OBJECT(mNativeData), "about-to-show",
                     G_CALLBACK(menu_about_to_show_cb), this);
    g_signal_connect(G_OBJECT(mNativeData), "event",
                     G_CALLBACK(menu_event_cb), this);
}

void
nsMenu::Refresh(nsMenuObject::ERefreshType aType)
{
    if (aType == nsMenuObject::eRefreshType_Full) {
        SyncLabelFromContent();
        SyncSensitivityFromContent();
    }

    SyncVisibilityFromContent();
    SyncIconFromContent();
}

void
nsMenu::InitializePopup()
{
    nsCOMPtr<nsIContent> oldPopupContent;
    oldPopupContent.swap(mPopupContent);

    for (uint32_t i = 0; i < mContent->GetChildCount(); ++i) {
        nsIContent *child = mContent->GetChildAt(i);

        int32_t dummy;
        nsCOMPtr<nsIAtom> tag = child->OwnerDoc()->BindingManager()->ResolveTag(child, &dummy);
        if (tag == nsGkAtoms::menupopup) {
            mPopupContent = child;
            break;
        }
    }

    if (oldPopupContent == mPopupContent) {
        return;
    }

    if (oldPopupContent && oldPopupContent != mContent) {
        mListener->UnregisterForContentChanges(oldPopupContent, this);
    }

    if (!mPopupContent) {
        return;
    }

    SetPopupState(ePopupState_Closed);

    nsCOMPtr<nsIXPConnect> xpconnect = services::GetXPConnect();
    nsIScriptGlobalObject *sgo = mPopupContent->OwnerDoc()->GetScriptGlobalObject();
    if (sgo) {
        nsCOMPtr<nsIScriptContext> scriptContext = sgo->GetContext();
        JSObject *global = sgo->GetGlobalJSObject();
        if (scriptContext && global) {
            AutoPushJSContext cx(scriptContext->GetNativeContext());
            if (cx) {
                nsCOMPtr<nsIXPConnectJSObjectHolder> wrapper;
                xpconnect->WrapNative(cx, global, mPopupContent,
                                      NS_GET_IID(nsISupports),
                                      getter_AddRefs(wrapper));
            }
        }
    }

    if (mPopupContent != mContent) {
        mListener->RegisterForContentChanges(mPopupContent, this);
    }
}

nsresult
nsMenu::RemoveMenuObjectAt(uint32_t aIndex)
{
    if (aIndex >= mMenuObjects.Length()) {
        return NS_ERROR_INVALID_ARG;
    }

    gboolean res = TRUE;
    if (!IsInUpdateBatch()) {
        res = dbusmenu_menuitem_child_delete(mNativeData,
                                             mMenuObjects[aIndex]->GetNativeData());
    }

    SetStructureMutatedFlag();

    mMenuObjects.RemoveElementAt(aIndex);

    return res ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsMenu::RemoveMenuObject(nsIContent *aChild)
{
    uint32_t index = IndexOf(aChild);
    if (index == NoIndex) {
        return NS_ERROR_INVALID_ARG;
    }

    return RemoveMenuObjectAt(index);
}

nsresult
nsMenu::InsertMenuObjectAfter(nsMenuObject *aChild, nsIContent *aPrevSibling)
{
    uint32_t index = IndexOf(aPrevSibling);
    if (index == NoIndex && aPrevSibling) {
        return NS_ERROR_INVALID_ARG;
    }

    ++index;

    gboolean res = TRUE;
    if (!IsInUpdateBatch()) {
        aChild->CreateNativeData();
        res = dbusmenu_menuitem_child_add_position(mNativeData,
                                                   aChild->GetNativeData(),
                                                   index);
    }

    SetStructureMutatedFlag();

    return res && mMenuObjects.InsertElementAt(index, aChild) ?
        NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsMenu::AppendMenuObject(nsMenuObject *aChild)
{
    gboolean res = TRUE;
    if (!IsInUpdateBatch()) {
        aChild->CreateNativeData();
        res = dbusmenu_menuitem_child_append(mNativeData,
                                             aChild->GetNativeData());
    }

    SetStructureMutatedFlag();

    return res && mMenuObjects.AppendElement(aChild) ? NS_OK : NS_ERROR_FAILURE;
}

bool
nsMenu::CanOpen() const
{
    bool isVisible = dbusmenu_menuitem_property_get_bool(mNativeData,
                                                         DBUSMENU_MENUITEM_PROP_VISIBLE);
    bool isDisabled = mContent->AttrValueIs(kNameSpaceID_None,
                                            nsGkAtoms::disabled,
                                            nsGkAtoms::_true,
                                            eCaseMatters);

    return (isVisible && !isDisabled);
}

nsMenu::nsMenu() :
    nsMenuObjectContainer(),
    mFlags(0)
{
    MOZ_COUNT_CTOR(nsMenu);
}

nsresult
nsMenu::ImplInit()
{
    SetNeedsRebuildFlag();

    if (mParent == MenuBar() && !MenuBar()->IsRegistered()) {
        SetIgnoreFirstAboutToShowFlag();
    }

    return NS_OK;
}

nsMenu::~nsMenu()
{
    if (IsInUpdateBatch()) {
        EndUpdateBatch();
    }

    // Although nsTArray will take care of this in its destructor,
    // we have to manually ensure children are removed from our native menu
    // item, just in case our parent is in an update batch and is going to
    // reuse our native menu item for something else
    while (mMenuObjects.Length() > 0) {
        RemoveMenuObjectAt(0);
    }

    if (mListener && mPopupContent && mContent != mPopupContent) {
        mListener->UnregisterForContentChanges(mPopupContent, this);
    }

    if (mNativeData) {
        g_signal_handlers_disconnect_by_func(mNativeData,
                                             nsNativeMenuUtils::FuncToVoidPtr(menu_about_to_show_cb),
                                             this);
        g_signal_handlers_disconnect_by_func(mNativeData,
                                             nsNativeMenuUtils::FuncToVoidPtr(menu_event_cb),
                                             this);
    }

    MOZ_COUNT_DTOR(nsMenu);
}

/* static */ already_AddRefed<nsMenuObject>
nsMenu::Create(nsMenuObject *aParent,
               nsIContent *aContent)
{
    nsRefPtr<nsMenu> menu = new nsMenu();
    if (NS_FAILED(menu->Init(aParent, aContent))) {
        return nullptr;
    }

    return menu.forget();
}

static void
DoOpen(nsITimer *aTimer, void *aClosure)
{
    nsMenu *menu = static_cast<nsMenu *>(aClosure);
    dbusmenu_menuitem_show_to_user(menu->GetNativeData(), 0);

    NS_RELEASE(aTimer);
    NS_RELEASE(menu);
}

void
nsMenu::OpenMenuDelayed()
{
    if (!CanOpen()) {
        return;
    }

    // Here, we synchronously fire popupshowing and popupshown events and then
    // open the menu after a short delay. This allows the menu to refresh before
    // it's shown, and avoids an issue where keyboard focus is not on the first
    // item of the history menu in Firefox when opening it with the keyboard,
    // because extra items to appear at the top of the menu

    SetPopupState(ePopupState_Closed);
    AboutToOpen(eAboutToOpenOrigin_FromAboutToShowSignal);
    SetPopupState(ePopupState_Closed);

    nsCOMPtr<nsITimer> timer = do_CreateInstance(NS_TIMER_CONTRACTID);
    if (!timer) {
        return;
    }

    if (NS_FAILED(timer->InitWithFuncCallback(DoOpen, this, 100,
                                              nsITimer::TYPE_ONE_SHOT))) {
        return;
    }

    NS_ADDREF(this);
    timer.forget();
}

void
nsMenu::OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute)
{
    NS_ASSERTION(aContent == mContent || aContent == mPopupContent,
                 "Received an event that wasn't meant for us!");

    if (aAttribute == nsGkAtoms::open) {
        return;
    }

    if (aContent == mContent) {
        if (aAttribute == nsGkAtoms::disabled) {
            SyncSensitivityFromContent();
        } else if (aAttribute == nsGkAtoms::label || 
                   aAttribute == nsGkAtoms::accesskey ||
                   aAttribute == nsGkAtoms::crop) {
            SyncLabelFromContent();
        } else if (aAttribute == nsGkAtoms::image) {
            SyncIconFromContent();
        } else if (aAttribute == nsGkAtoms::hidden ||
                   aAttribute == nsGkAtoms::collapsed) {
            SyncVisibilityFromContent();
        }
    }
}

void
nsMenu::OnContentInserted(nsIContent *aContainer, nsIContent *aChild,
                          nsIContent *aPrevSibling)
{
    NS_ASSERTION(aContainer == mContent || aContainer == mPopupContent,
                 "Received an event that wasn't meant for us!");

    if (NeedsRebuild()) {
        return;
    }

    nsresult rv;
    if (aContainer == mPopupContent) {
        nsRefPtr<nsMenuObject> child = CreateChild(aChild, &rv);

        if (child) {
            rv = InsertMenuObjectAfter(child, aPrevSibling);
        }
    } else {
        rv = Build();
    }

    if (NS_FAILED(rv)) {
        NS_WARNING("OnContentInserted failed - marking invalid");
        SetNeedsRebuildFlag();
    }
}

void
nsMenu::OnContentRemoved(nsIContent *aContainer, nsIContent *aChild)
{
    NS_ASSERTION(aContainer == mContent || aContainer == mPopupContent,
                 "Received an event that wasn't meant for us!");

    if (NeedsRebuild()) {
        return;
    }

    nsresult rv;
    if (aContainer == mPopupContent) {
        rv = RemoveMenuObject(aChild);
    } else {
        rv = Build();
    }

    if (NS_FAILED(rv)) {
        NS_WARNING("OnContentRemoved failed - marking invalid");
        SetNeedsRebuildFlag();
    }
}

/*
 * Some menus (eg, the History menu in Firefox) refresh themselves on
 * opening by removing all children and then re-adding new ones. As this
 * happens whilst the menu is opening in Unity, it causes some flickering
 * as the menu popup is resized multiple times. To avoid this, we try to
 * reuse native menu items when the menu structure changes during a
 * batched update. If we can handle menu structure changes from Gecko
 * just by updating properties of native menu items (rather than destroying
 * and creating new ones), then we eliminate any flickering that occurs as
 * the menu is opened. To do this, we don't modify any native menu items
 * until the end of the update batch.
 */

void
nsMenu::BeginUpdateBatch(nsIContent *aContent)
{
    NS_ASSERTION(aContent == mContent || aContent == mPopupContent,
                 "Received an event that wasn't meant for us!");

    if (aContent == mPopupContent) {
        NS_ASSERTION(!IsInUpdateBatch(), "Already in an update batch!");

        SetIsInUpdateBatchFlag();
        ClearStructureMutatedFlag();
    }
}

void
nsMenu::EndUpdateBatch()
{
    NS_ASSERTION(IsInUpdateBatch(), "Not in an update batch");

    ClearIsInUpdateBatchFlag();

    /* Optimize for the case where we only had attribute changes */
    if (!DidStructureMutate()) {
        return;
    }

    GList *nextNativeChild = dbusmenu_menuitem_get_children(mNativeData);
    DbusmenuMenuitem *nextOwnedNativeChild = nullptr;

    uint32_t count = mMenuObjects.Length();

    // Find the first native menu item that is `owned` by a corresponding
    // Gecko menuitem
    for (uint32_t i = 0; i < count; ++i) {
        if (mMenuObjects[i]->GetNativeData()) {
            nextOwnedNativeChild = mMenuObjects[i]->GetNativeData();
            break;
        }
    }

    // Now iterate over all Gecko menuitems
    for (uint32_t i = 0; i < count; ++i) {
        nsMenuObject *child = mMenuObjects[i];

        if (child->GetNativeData()) {
            // This child already has a corresponding native menuitem.
            // Remove all preceding orphaned native items. At this point, we
            // modify the native menu structure.
            while (nextNativeChild &&
                   nextNativeChild->data != nextOwnedNativeChild) {
                DbusmenuMenuitem *data =
                    static_cast<DbusmenuMenuitem *>(nextNativeChild->data);
                nextNativeChild = nextNativeChild->next;
                dbusmenu_menuitem_child_delete(mNativeData, data);
            }

            if (nextNativeChild) {
                nextNativeChild = nextNativeChild->next;
            }

            // Now find the next native menu item that is `owned`
            nextOwnedNativeChild = nullptr;
            for (uint32_t j = i + 1; j < count; ++j) {
                if (mMenuObjects[j]->GetNativeData()) {
                    nextOwnedNativeChild = mMenuObjects[j]->GetNativeData();
                    break;
                }
            }
        } else {
            // This child is new, and doesn't have a native menu item. Find one!
            if (nextNativeChild && nextNativeChild->data != nextOwnedNativeChild) {
                DbusmenuMenuitem *data =
                    static_cast<DbusmenuMenuitem *>(nextNativeChild->data);
                if (NS_SUCCEEDED(child->AdoptNativeData(data))) {
                    nextNativeChild = nextNativeChild->next;
                }
            }

            // There wasn't a suitable one available, so create a new one.
            // At this point, we modify the native menu structure.
            if (!child->GetNativeData()) {
                child->CreateNativeData();
                dbusmenu_menuitem_child_add_position(mNativeData,
                                                     child->GetNativeData(),
                                                     i);
            }
        }
    }

    while (nextNativeChild) {
        DbusmenuMenuitem *data =
            static_cast<DbusmenuMenuitem *>(nextNativeChild->data);
        nextNativeChild = nextNativeChild->next;
        dbusmenu_menuitem_child_delete(mNativeData, data);
    }

    ClearStructureMutatedFlag();
}
