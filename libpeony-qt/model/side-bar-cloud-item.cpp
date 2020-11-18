        #include "side-bar-cloud-item.h"


#include "side-bar-model.h"
#include "file-utils.h"
#include <QStandardPaths>

using namespace Peony;

SideBarCloudItem::SideBarCloudItem(QString uri,
        SideBarCloudItem *parentItem,
        SideBarModel *model,
        QObject *parent) : SideBarAbstractItem (model, parent)
{
    m_parent = parentItem;
    m_is_root_child = parentItem == nullptr;
    if (m_is_root_child) {
        QString homeUri = "cloud:///";
        m_uri = homeUri;
        m_display_name = tr("CloudStorage");
        //m_icon_name = "emblem-personal";
        //top dir don't show icon
        m_icon_name = "";

        QString documentUri = "file:///home/";
        SideBarCloudItem *documentItem = new SideBarCloudItem(documentUri,
                this,
                m_model);
        m_children->append(documentItem);

        m_model->insertRows(0, 5, firstColumnIndex());
        return;
    }
    m_uri = uri;
    m_display_name = tr("CloudFile");
    m_icon_name = "ukui-cloud-file";// FileUtils::getFileIconName(uri);
}

QModelIndex SideBarCloudItem::firstColumnIndex()
{
    return m_model->firstCloumnIndex(this);
}

QModelIndex SideBarCloudItem::lastColumnIndex()
{
    return m_model->lastCloumnIndex(this);
}