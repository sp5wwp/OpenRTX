/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/UiMemory.hpp"

#include <cstdlib>
#include <cstring>

namespace ortxui
{

UiMemory uiMemory;

UiMemory::UiMemory() :
    lists(), edit(), inlineEdit(), draft(nullptr), inboxPinned(0),
    messageSeq(0), messageScroll(0), draftReply(false)
{
    inlineEdit.owner = ViewId::Count;
}

UiMemory::~UiMemory()
{
    closeDraft();
    closeEdit();
}

bool UiMemory::openDraft(const char *recipient, bool reply)
{
    if (draft == nullptr)
        draft = static_cast<ComposeDraft *>(malloc(sizeof(ComposeDraft)));
    if (draft == nullptr)
        return false;

    memset(draft->recipient, 0, sizeof(draft->recipient));
    if (recipient != nullptr)
        strncpy(draft->recipient, recipient, sizeof(draft->recipient) - 1);
    draft->body[0] = '\0';
    draftReply = reply;
    return true;
}

void UiMemory::closeDraft()
{
    free(draft);
    draft = nullptr;
}

bool UiMemory::openEdit(const UiText &title, char *dst, uint16_t cap,
                        const Charset &cs, const MultiTapTable &tap,
                        bool multiline, bool rtx)
{
    free(edit.work);
    edit.work = static_cast<char *>(malloc(cap));
    if (edit.work == nullptr)
        return false;

    strncpy(edit.work, dst, cap - 1);
    edit.work[cap - 1] = '\0';

    edit.title = title;
    edit.dst = dst;
    edit.cap = cap;
    edit.charset = &cs;
    edit.tap = &tap;
    edit.multiline = multiline;
    edit.rtx = rtx;
    edit.cursor = 0;
    edit.topRow = 0;
    edit.picked = 0;
    edit.pickerSelected = 0;
    edit.pickerTop = 0;
    return true;
}

void UiMemory::closeEdit()
{
    free(edit.work);
    edit.work = nullptr;
}

} // namespace ortxui
