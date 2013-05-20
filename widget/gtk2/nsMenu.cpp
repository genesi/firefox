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

class nsSetAttrRunnableNoNotify : public nsRunnable {
public:
    nsSetAttrRunnableNoNotify(nsIContent *aContent, nsIAtom *aAttribute,
                              nsAString& aValue) :
                              mContent(aContent), mAttribute(aAttribute),
                              mValue(aValue) { };

    NS_IMETHODIMP Run() {
        return mContent->SetAttr(kNameSpaceID_None, mAttribute, mValue, false);
    }

private:
    nsCOMPtr<nsIContent> mContent;
    nsCOMPtr<nsIAtom> mAttribute;
    nsAutoString mValue;
};

class nsUnsetAttrRunnableNoNotify : public nsRunnable {
public:
    nsUnsetAttrRunnableNoNotify(nsIContent *aContent, nsIAtom *aAttribute) :
                                mContent(aContent), mAttribute(aAttribute) { };

    NS_IMETHODIMP Run() {
        return mContent->UnsetAttr(kNameSpaceID_None, mAttribute, false);
    }

private:
    nsCOMPtr<nsIContent> mContent;
    nsCOMPtr<nsIAtom> mAttribute;
};

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
        case ePopupState_Open:
            state.Assign(NS_LITERAL_STRING("open"));
            break;
        case ePopupState_Hiding:
            state.Assign(NS_LITERAL_STRING("hiding"));
            break;
        default:
            break;
    }

    if (nsContentUtils::IsSafeToRunScript()) {
        if (state.IsEmpty()) {
            mPopupContent->UnsetAttr(kNameSpaceID_None,
                                     nsNativeMenuAtoms::_moz_menupopupstate,
                                     false);
        } else {
            mPopupContent->SetAttr(kNameSpaceID_None,
                                   nsNativeMenuAtoms::_moz_menupopupstate,
                                   state, false);
        }
    } else {
        nsCOMPtr<nsIRunnable> r;
        if (state.IsEmpty()) {
            r = new nsUnsetAttrRunnableNoNotify(
                        mPopupContent, nsNativeMenuAtoms::_moz_menupopupstate);
        } else {
            r = new nsSetAttrRunnableNoNotify(
                        mPopupContent, nsNativeMenuAtoms::_moz_menupopupstate,
                        state);
        }
        nsContentUtils::AddScriptRunner(r);
    }
}

/* static */ gboolean
nsMenu::menu_about_to_show_cb(DbusmenuMenuitem *menu,
                              gpointer user_data)
{
    nsMenu *self = static_cast<nsMenu *>(user_data);

    self->AboutToOpen();

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
        self->AboutToOpen();
        return;
    }
}

void
nsMenu::MaybeAddPlaceholderItem()
{
    NS_ASSERTION(!IsInUpdateBatch(),
                 "Shouldn't be modifying the native menu structure now");

    GList *children = dbusmenu_menuitem_get_children(mNativeData);
    if (!children) {
        NS_ASSERTION(!HasPlaceholderItem(), "Huh?");

        DbusmenuMenuitem *ph = dbusmenu_menuitem_new();
        if (!ph) {
            return;
        }

        dbusmenu_menuitem_property_set_bool(
            ph, DBUSMENU_MENUITEM_PROP_VISIBLE, false);

        if (!dbusmenu_menuitem_child_append(mNativeData, ph)) {
            NS_WARNING("Failed to create placeholder item");
            g_object_unref(ph);
            return;
        }

        g_object_unref(ph);

        SetHasPlaceholderItemFlag();
    }
}

bool
nsMenu::EnsureNoPlaceholderItem()
{
    NS_ASSERTION(!IsInUpdateBatch(),
                 "Shouldn't be modifying the native menu structure now");

    if (HasPlaceholderItem()) {
        GList *children = dbusmenu_menuitem_get_children(mNativeData);

        NS_ASSERTION(g_list_length(children) == 1,
                     "Unexpected number of children in native menu (should be 1!)");

        ClearHasPlaceholderItemFlag();

        if (!children) {
            return true;
        }

        if (!dbusmenu_menuitem_child_delete(
                mNativeData, static_cast<DbusmenuMenuitem *>(children->data))) {
            NS_ERROR("Failed to remove placeholder item");
            return false;
        }
    }

    return true;
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
nsMenu::AboutToOpen()
{
    if (PopupState() == ePopupState_Open) {
        return;
    }

    if (NeedsBuild() && NS_FAILED(Build())) {
        SetNeedsBuildFlag();
        return;
    }

    nsCOMPtr<nsIContent> popupContent(mPopupContent);
    {
        nsNativeMenuAutoUpdateBatch batch;

        SetPopupState(ePopupState_Showing);
        DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_SHOWING);

        mContent->SetAttr(kNameSpaceID_None, nsGkAtoms::open,
                          NS_LITERAL_STRING("true"), true);
    }

    uint32_t count = mMenuObjects.Length();
    for (uint32_t i = 0; i < count; ++i) {
        mMenuObjects[i]->Update();
    }

    // I guess that the popup could have changed
    if (popupContent == mPopupContent) {
        nsNativeMenuAutoUpdateBatch batch;
        SetPopupState(ePopupState_Open);
        DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_SHOWN);
    }
}

nsresult
nsMenu::Build()
{
    while (mMenuObjects.Length() > 0) {
        RemoveMenuObjectAt(0);
    }

    if (NeedsBuild() && !HasPlaceholderItem()) {
        NS_ASSERTION(!IsInUpdateBatch(), "How did we end up here?");

        // Make sure there are no orphaned children. We skip this if
        // we have a placeholder item. In this case, we only add a placeholder
        // when there are no other children, and assert when removing it that
        // there are no other children.
        GList *children = dbusmenu_menuitem_take_children(mNativeData);
        g_list_foreach(children, (GFunc)g_object_unref, nullptr);
        g_list_free(children);
    }

    InitializePopup();

    if (!mPopupContent) {
        return NS_OK;
    }

    ClearNeedsBuildFlag();

    uint32_t count = mPopupContent->GetChildCount();
    for (uint32_t i = 0; i < count; ++i) {
        nsIContent *childContent = mPopupContent->GetChildAt(i);

        nsresult rv;
        nsRefPtr<nsMenuObject> child = CreateChild(childContent, &rv);

        if (child) {
            rv = AppendMenuObject(child);
        }

        if (NS_FAILED(rv)) {
            NS_ERROR("Menu build failed");
            return rv;
        }
    }

    return NS_OK;
}

void
nsMenu::InitializeNativeData()
{
    g_signal_connect(G_OBJECT(mNativeData), "about-to-show",
                     G_CALLBACK(menu_about_to_show_cb), this);
    g_signal_connect(G_OBJECT(mNativeData), "event",
                     G_CALLBACK(menu_event_cb), this);

    SyncLabelFromContent();
    SyncSensitivityFromContent();

    SetNeedsBuildFlag();
    MaybeAddPlaceholderItem();
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
        mListener->UnregisterForContentChanges(oldPopupContent);
    }

    SetPopupState(ePopupState_Closed);

    if (!mPopupContent) {
        return;
    }

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

    if (!IsInUpdateBatch()) {
        NS_ASSERTION(!HasPlaceholderItem(), "Shouldn't have a placeholder menuitem");
        if (!dbusmenu_menuitem_child_delete(
                mNativeData, mMenuObjects[aIndex]->GetNativeData())) {
            return NS_ERROR_FAILURE;
        }
    }

    SetStructureMutatedFlag();

    mMenuObjects.RemoveElementAt(aIndex);

    if (!IsInUpdateBatch()) {
        MaybeAddPlaceholderItem();
    }

    return NS_OK;
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

    if (!IsInUpdateBatch()) {
        if (!EnsureNoPlaceholderItem()) {
            return NS_ERROR_FAILURE;
        }

        aChild->CreateNativeData();
        if (!dbusmenu_menuitem_child_add_position(mNativeData,
                                                  aChild->GetNativeData(),
                                                  index)) {
            return NS_ERROR_FAILURE;
        }
    }

    SetStructureMutatedFlag();

    return mMenuObjects.InsertElementAt(index, aChild) ?
        NS_OK : NS_ERROR_FAILURE;
}

nsresult
nsMenu::AppendMenuObject(nsMenuObject *aChild)
{
    if (!IsInUpdateBatch()) {
        if (!EnsureNoPlaceholderItem()) {
            return NS_ERROR_FAILURE;
        }

        aChild->CreateNativeData();
        if (!dbusmenu_menuitem_child_append(mNativeData,
                                            aChild->GetNativeData())) {
            return NS_ERROR_FAILURE;
        }
    }

    SetStructureMutatedFlag();

    return mMenuObjects.AppendElement(aChild) ? NS_OK : NS_ERROR_FAILURE;
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

    EnsureNoPlaceholderItem();

    if (mListener && mPopupContent && mContent != mPopupContent) {
        mListener->UnregisterForContentChanges(mPopupContent);
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
nsMenu::Create(nsMenuObjectContainer *aParent,
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
    AboutToOpen();
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
nsMenu::OnClose()
{
    if (PopupState() == ePopupState_Closed) {
        return;
    }

    // We do this here as an alternative to holding a strong
    // reference to ourselves (and also mPopupContent)
    nsNativeMenuAutoUpdateBatch batch;

    SetPopupState(ePopupState_Hiding);

    DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_HIDING);
    SetPopupState(ePopupState_Closed);

    DispatchMouseEvent(mPopupContent, NS_XUL_POPUP_HIDDEN);

    mContent->UnsetAttr(kNameSpaceID_None, nsGkAtoms::open, true);
}

void
nsMenu::Update()
{
    SyncVisibilityFromContent();
    SyncIconFromContent();
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

    if (NeedsBuild()) {
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
        NS_ERROR("OnContentInserted() failed");
        SetNeedsBuildFlag();
    }
}

void
nsMenu::OnContentRemoved(nsIContent *aContainer, nsIContent *aChild)
{
    NS_ASSERTION(aContainer == mContent || aContainer == mPopupContent,
                 "Received an event that wasn't meant for us!");

    if (NeedsBuild()) {
        return;
    }

    nsresult rv;
    if (aContainer == mPopupContent) {
        rv = RemoveMenuObject(aChild);
    } else {
        rv = Build();
    }

    if (NS_FAILED(rv)) {
        NS_ERROR("OnContentRemoved() failed");
        SetNeedsBuildFlag();
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

    if (!EnsureNoPlaceholderItem()) {
        SetNeedsBuildFlag();
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

                if (!dbusmenu_menuitem_child_delete(mNativeData, data)) {
                    NS_ERROR("Failed to remove orphaned native item from menu");
                    SetNeedsBuildFlag();
                    return;
                }
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
            if (nextNativeChild &&
                nextNativeChild->data != nextOwnedNativeChild) {

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
                if (!dbusmenu_menuitem_child_add_position(mNativeData,
                                                          child->GetNativeData(),
                                                          i)) {
                    NS_ERROR("Failed to add new native item");
                    SetNeedsBuildFlag();
                    return;
                }
            }
        }
    }

    while (nextNativeChild) {

        DbusmenuMenuitem *data =
            static_cast<DbusmenuMenuitem *>(nextNativeChild->data);
        nextNativeChild = nextNativeChild->next;

        if (!dbusmenu_menuitem_child_delete(mNativeData, data)) {
            NS_ERROR("Failed to remove orphaned native item from menu");
            SetNeedsBuildFlag();
            return;
        }
    }

    MaybeAddPlaceholderItem();
}
