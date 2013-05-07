/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsMenuSeparator_h__
#define __nsMenuSeparator_h__

#include "mozilla/Attributes.h"

#include "nsMenuObject.h"

class nsIContent;
class nsIAtom;

// Menu separator class
class nsMenuSeparator MOZ_FINAL : public nsMenuObject
{
public:
    ~nsMenuSeparator();

    nsMenuObject::EType Type() const {
        return nsMenuObject::eType_MenuSeparator;
    }

    static already_AddRefed<nsMenuObject> Create(nsMenuObject *aParent,
                                                 nsIContent *aContent);

    void OnAttributeChanged(nsIContent *aContent, nsIAtom *aAttribute);

private:
    nsMenuSeparator();

    void InitializeNativeData();
    void Refresh(nsMenuObject::ERefreshType aType);
    bool IsCompatibleWithNativeData(DbusmenuMenuitem *aNativeData);

    nsMenuObject::PropertyFlags SupportedProperties() const {
        return nsMenuObject::PropertyFlags(
            nsMenuObject::ePropVisible |
            nsMenuObject::ePropType
        );
    };
};

#endif /* __nsMenuSeparator_h__ */
