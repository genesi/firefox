/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMenuSeparator.h"
#include "nsIContent.h"
#include "nsIAtom.h"
#include "nsCRT.h"

#include "nsDbusmenu.h"

void
nsMenuSeparator::InitializeNativeData()
{
    dbusmenu_menuitem_property_set(mNativeData,
                                   DBUSMENU_MENUITEM_PROP_TYPE,
                                   "separator");
}

bool
nsMenuSeparator::IsCompatibleWithNativeData(DbusmenuMenuitem *aNativeData)
{
    return nsCRT::strcmp(dbusmenu_menuitem_property_get(aNativeData,
                                                        DBUSMENU_MENUITEM_PROP_TYPE),
                         "separator") == 0;
}

nsMenuSeparator::nsMenuSeparator()
{
    MOZ_COUNT_CTOR(nsMenuSeparator);
}

nsMenuSeparator::~nsMenuSeparator()
{
    MOZ_COUNT_DTOR(nsMenuSeparator);
}

/* static */ already_AddRefed<nsMenuObject>
nsMenuSeparator::Create(nsMenuObjectContainer *aParent,
                        nsIContent *aContent)
{
    nsRefPtr<nsMenuSeparator> sep = new nsMenuSeparator();
    if (NS_FAILED(sep->Init(aParent, aContent))) {
        return nullptr;
    }

    return sep.forget();
}

void
nsMenuSeparator::Update()
{
    SyncVisibilityFromContent();
}

void
nsMenuSeparator::OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute)
{
    NS_ASSERTION(aContent == mContent, "Received an event that wasn't meant for us!");

    SyncVisibilityFromContent();
}
