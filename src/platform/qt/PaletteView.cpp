/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "PaletteView.h"

#include "GBAApp.h"
#include "LogController.h"
#include "VFileDevice.h"

#include <QFileDialog>
#include <QFontDatabase>

extern "C" {
#include "core/core.h"
#include "util/export.h"
#ifdef M_CORE_GA
#include "gba/gba.h"
#endif
#ifdef M_CORE_GB
#include "gb/gb.h"
#endif
#include "util/vfs.h"
}

using namespace QGBA;

PaletteView::PaletteView(GameController* controller, QWidget* parent)
	: QWidget(parent)
	, m_controller(controller)
{
	m_ui.setupUi(this);

	connect(m_controller, SIGNAL(frameAvailable(const uint32_t*)), this, SLOT(updatePalette()));
	m_ui.bgGrid->setDimensions(QSize(16, 16));
	m_ui.objGrid->setDimensions(QSize(16, 16));
	int count = 256;
#ifdef M_CORE_GB
	if (controller->platform() == PLATFORM_GB) {
		m_ui.bgGrid->setDimensions(QSize(4, 8));
		m_ui.objGrid->setDimensions(QSize(4, 8));
		m_ui.bgGrid->setSize(24);
		m_ui.objGrid->setSize(24);
		count = 32;
	}
#endif
	m_ui.selected->setSize(64);
	m_ui.selected->setDimensions(QSize(1, 1));
	updatePalette();

	const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);

	m_ui.hexcode->setFont(font);
	m_ui.value->setFont(font);
	m_ui.index->setFont(font);
	m_ui.r->setFont(font);
	m_ui.g->setFont(font);
	m_ui.b->setFont(font);

	connect(m_ui.bgGrid, SIGNAL(indexPressed(int)), this, SLOT(selectIndex(int)));
	connect(m_ui.objGrid, &Swatch::indexPressed, [this, count] (int index) { selectIndex(index + count); });
	connect(m_ui.exportBG, &QAbstractButton::clicked, [this, count] () { exportPalette(0, count); });
	connect(m_ui.exportOBJ, &QAbstractButton::clicked, [this, count] () { exportPalette(count, count); });

	connect(controller, SIGNAL(gameStopped(mCoreThread*)), this, SLOT(close()));
}

void PaletteView::updatePalette() {
	if (!m_controller->thread() || !m_controller->thread()->core) {
		return;
	}
	const uint16_t* palette;
	size_t count;
	switch (m_controller->platform()) {
#ifdef M_CORE_GBA
	case PLATFORM_GBA:
		palette = static_cast<GBA*>(m_controller->thread()->core->board)->video.palette;
		count = 256;
		break;
#endif
#ifdef M_CORE_GB
	case PLATFORM_GB:
		palette = static_cast<GB*>(m_controller->thread()->core->board)->video.palette;
		count = 32;
		break;
#endif
	default:
		return;
	}
	for (int i = 0; i < count; ++i) {
		m_ui.bgGrid->setColor(i, palette[i]);
		m_ui.objGrid->setColor(i, palette[i + count]);
	}
	m_ui.bgGrid->update();
	m_ui.objGrid->update();
}

void PaletteView::selectIndex(int index) {
	const uint16_t* palette;
	switch (m_controller->platform()) {
#ifdef M_CORE_GBA
	case PLATFORM_GBA:
		palette = static_cast<GBA*>(m_controller->thread()->core->board)->video.palette;
		break;
#endif
#ifdef M_CORE_GB
	case PLATFORM_GB:
		palette = static_cast<GB*>(m_controller->thread()->core->board)->video.palette;
		break;
#endif
	default:
		return;
	}
	uint16_t color = palette[index];
	m_ui.selected->setColor(0, color);
	uint32_t r = GBA_R5(color);
	uint32_t g = GBA_G5(color);
	uint32_t b = GBA_B5(color);
	uint32_t hexcode = (r << 19) | (g << 11) | (b << 3);
	hexcode |= (hexcode >> 5) & 0x070707;
	m_ui.hexcode->setText(tr("#%0").arg(hexcode, 6, 16, QChar('0')));
	m_ui.value->setText(tr("0x%0").arg(color, 4, 16, QChar('0')));
	m_ui.index->setText(tr("%0").arg(index, 3, 10, QChar('0')));
	m_ui.r->setText(tr("0x%0 (%1)").arg(r, 2, 16, QChar('0')).arg(r, 2, 10, QChar('0')));
	m_ui.g->setText(tr("0x%0 (%1)").arg(g, 2, 16, QChar('0')).arg(g, 2, 10, QChar('0')));
	m_ui.b->setText(tr("0x%0 (%1)").arg(b, 2, 16, QChar('0')).arg(b, 2, 10, QChar('0')));
}

void PaletteView::exportPalette(int start, int length) {
	if (start >= 512) {
		return;
	}
	if (start + length > 512) {
		length = 512 - start;
	}
	m_controller->threadInterrupt();
	QFileDialog* dialog = GBAApp::app()->getSaveFileDialog(this, tr("Export palette"),
	                                                       tr("Windows PAL (*.pal);;Adobe Color Table (*.act)"));
	if (!dialog->exec()) {
		m_controller->threadContinue();
		return;
	}
	QString filename = dialog->selectedFiles()[0];
	VFile* vf = VFileDevice::open(filename, O_WRONLY | O_CREAT | O_TRUNC);
	if (!vf) {
		LOG(QT, ERROR) << tr("Failed to open output palette file: %1").arg(filename);
		m_controller->threadContinue();
		return;
	}
	QString filter = dialog->selectedNameFilter();
	if (filter.contains("*.pal")) {
		exportPaletteRIFF(vf, length, &static_cast<GBA*>(m_controller->thread()->core->board)->video.palette[start]);
	} else if (filter.contains("*.act")) {
		exportPaletteACT(vf, length, &static_cast<GBA*>(m_controller->thread()->core->board)->video.palette[start]);
	}
	vf->close(vf);
	m_controller->threadContinue();
}
