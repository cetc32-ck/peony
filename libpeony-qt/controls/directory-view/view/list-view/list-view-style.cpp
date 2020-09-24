/*
 * Peony-Qt's Library
 *
 * Copyright (C) 2020, Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: Yue Lan <lanyue@kylinos.cn>
 *
 */

#include "list-view-style.h"
#include "list-view-delegate.h"

#include <QStyleOption>
#include <QPainterPath>
#include <QPainter>
#include <QDebug>

using namespace Peony;
using namespace Peony::DirectoryView;

static ListViewStyle *global_instance = nullptr;

ListViewStyle::ListViewStyle(QObject *parent) : QProxyStyle()
{

}

ListViewStyle *ListViewStyle::getStyle()
{
    if (!global_instance)
        global_instance = new ListViewStyle;
    return global_instance;
}

void ListViewStyle::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == PE_Frame) {
        painter->save();
        bool isActive = option->state & State_Active;
        bool isEnable = option->state & State_Enabled;
        auto baseColor = option->palette.color(isEnable? (isActive? QPalette::Active: QPalette::Inactive): QPalette::Disabled, QPalette::Window);
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        path.addRoundedRect(option->rect, 16, 16);
        path.addRect(QRect(0, 0, 16, 16));
        path.addRect(QRect(option->rect.width()-16,0, 16, 16));
        path.addRect(0,option->rect.height()-16,16,16);
        if(widget){
            if(qobject_cast<const QTextEdit *>(widget)){
                path.addRect(QRect(option->rect.width()-16,option->rect.height()-16,16,16));
            }
        }
        painter->fillPath(path, option->palette.base().color());
        painter->restore();
        return;
    }

    if (element == PE_FrameWindow) {
        return;
    }

    return QProxyStyle::drawPrimitive(element, option, painter, widget);
}
