/*
	 Copyright (C) 2011 Georgia Institute of Technology, University of Utah, Weill Cornell Medical College

	 This program is free software: you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation, either version 3 of the License, or
	 (at your option) any later version.

	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <QtGui>
#include <QTimer>
#include <QPainter>
#include <QMdiSubWindow>

#include "oscilloscope.h"
#include <debug.h>
#include <main_window.h>
#include <rt.h>
#include <workspace.h>
#include <cmath>
#include <sstream>

namespace {
	class SyncEvent : public RT::Event {
		public:
			int callback(void) {
				return 0;
			};
	}; // class SyncEvent

	struct channel_info {
		QString name;
		IO::Block *block;
		IO::flags_t type;
		size_t index;
		double previous; // stores previous value for trigger and downsample buffer
	};
} // namespace

Oscilloscope::Properties::Properties(Oscilloscope::Panel *parent) : QDialog(MainWindow::getInstance()), panel(parent) {

	QBoxLayout *layout = new QVBoxLayout;

	// Create tab widget
	tabWidget = new QTabWidget;
	layout->addWidget(tabWidget);
	QObject::connect(tabWidget,SIGNAL(currentChanged(QWidget *)),this,SLOT(showTab(void)));

	/*QHBox *hbox = new QHBox(this);
		layout->addWidget(hbox);*/

	buttonGroup = new QGroupBox;
	QHBoxLayout *buttonLayout = new QHBoxLayout;

	QPushButton *applyButton = new QPushButton("Apply");
	QObject::connect(applyButton,SIGNAL(clicked(void)),this,SLOT(apply(void)));
	buttonLayout->addWidget(applyButton);
	QPushButton *okayButton = new QPushButton("Okay");
	QObject::connect(okayButton,SIGNAL(clicked(void)),this,SLOT(okay(void)));
	buttonLayout->addWidget(okayButton);
	QPushButton *cancelButton = new QPushButton("Cancel");
	QObject::connect(cancelButton,SIGNAL(clicked(void)),this,SLOT(close(void)));
	buttonLayout->addWidget(cancelButton);

	createChannelTab();
	//createDisplayTab();
	//createAdvancedTab();
	
	layout->addWidget(buttonGroup);
	setLayout(layout);
	setWindowTitle(QString::number(parent->getID()) + " Oscilloscope Properties");
}

Oscilloscope::Properties::~Properties(void)
{
}

void Oscilloscope::Properties::receiveEvent(const ::Event::Object *event) {
	if (event->getName() == Event::IO_BLOCK_INSERT_EVENT) {
		IO::Block *block =
			reinterpret_cast<IO::Block *> (event->getParam("block"));

		if (block) {
			// Update the list of blocks
			blockList->addItem(QString::fromStdString(block->getName())+" "+QString::number(block->getID()));
			panel->blocks.push_back(block);

			if (blockList->count() == 1)
				buildChannelList();
		}
	}
	else if (event->getName() == Event::IO_BLOCK_REMOVE_EVENT) {
		IO::Block *block =
			reinterpret_cast<IO::Block *> (event->getParam("block"));

		if (block) {
			// Find the index of the block in the blocks vector
			size_t index;
			for (index = 0; index < panel->blocks.size() && panel->blocks[index]
					!= block; ++index);

			if (index < panel->blocks.size()) {
				bool found = false;
				// Stop displaying channels coming from the removed block

				for (std::list<Scope::Channel>::iterator i =panel->getChannelsBegin(), end = panel->getChannelsEnd(); i != end;) {
					struct channel_info *info = reinterpret_cast<struct channel_info *> (i->getInfo());
					if (info->block == block)	{
						found = true;

						// If triggering on this channel disable triggering
						if (i->getLabel()	== panel->getTriggerChannel()->getLabel()){
							panel->setTrigger(Scope::NONE,
									panel->getTriggerThreshold(),
									panel->getChannelsEnd(),
									panel->getTriggerHolding(),
									panel->getTriggerHoldoff());
							showDisplayTab();
						}

						struct channel_info
							*info =
							reinterpret_cast<struct channel_info *> (i->getInfo());

						std::list<Scope::Channel>::iterator chan = i++;

						bool active = panel->setInactiveSync();
						panel->removeChannel(chan);
						panel->flushFifo();
						panel->setActive(active);

						delete info;
					}
					else
						++i;
				}

				// Update the list of blocks
				size_t current = blockList->currentIndex();
				blockList->removeItem(index);
				panel->blocks.erase(panel->blocks.begin() + index);

				if (current == index) {
					blockList->setCurrentIndex(0);
					buildChannelList();
				}

				showTab();
			}
			else
				DEBUG_MSG("Oscilloscope::Properties::receiveEvent : removed block never inserted\n");
		}
	}
	else if (event->getName() == Event::RT_POSTPERIOD_EVENT) {
		panel->setPeriod(RT::System::getInstance()->getPeriod() * 1e-6);
		panel->adjustDataSize();
		showTab();
	}
}

void Oscilloscope::Properties::closeEvent(QCloseEvent *e) {
	e->ignore();
	hide();
}

void Oscilloscope::Properties::activateChannel(bool active) {
	bool enable = active && blockList->count() && channelList->count();
	displayBox->setEnabled(enable);
	lineBox->setEnabled(enable);
}

void Oscilloscope::Properties::apply(void) {
	switch (tabWidget->currentIndex()) {
		case 0:
			applyChannelTab();
			break;
		case 1:
			applyDisplayTab();
			break;
		case 2:
			applyAdvancedTab();
			break;
		default:
			ERROR_MSG("Oscilloscope::Properties::showTab : invalid tab\n");
	}
}

void Oscilloscope::Properties::buildChannelList(void) {
	channelList->clear();
	if (!blockList->count())
		return;

	if (blockList->currentIndex() < 0)
		blockList->setCurrentIndex(0);

	IO::Block *block = panel->blocks[blockList->currentIndex()];
	IO::flags_t type;
	switch (typeList->currentIndex())
	{
		case 0:
			type = Workspace::INPUT;
			break;
		case 1:
			type = Workspace::OUTPUT;
			break;
		case 2:
			type = Workspace::PARAMETER;
			break;
		case 3:
			type = Workspace::STATE;
			break;
		default:
			ERROR_MSG("Oscilloscope::Properties::buildChannelList : invalid type selection\n");
			type = Workspace::INPUT;
	}

	for (size_t i = 0; i < block->getCount(type); ++i)
		channelList->addItem(QString::fromStdString(block->getName(type, i)));

	showChannelTab();
}

void Oscilloscope::Properties::okay(void) {
	apply();
	close();
}

void Oscilloscope::Properties::showTab(void) {
	switch (tabWidget->currentIndex()) {
		case 0:
			showChannelTab();
			break;
		case 1:
			showDisplayTab();
			break;
		case 2:
			showAdvancedTab();
			break;
		default:
			ERROR_MSG("Oscilloscope::Properties::showTab : invalid tab\n");
	}
}

void Oscilloscope::Properties::applyAdvancedTab(void) {
	panel->setDivXY(divXSpin->value(), divYSpin->value());

	panel->adjustDataSize();

	showAdvancedTab();
}

void Oscilloscope::Properties::applyChannelTab(void) {
	if (blockList->count() <= 0 || channelList->count() <= 0)
		return;

	IO::Block *block = panel->blocks[blockList->currentIndex()];
	IO::flags_t type;
	switch (typeList->currentIndex()) {
		case 0:
			type = Workspace::INPUT;
			break;
		case 1:
			type = Workspace::OUTPUT;
			break;
		case 2:
			type = Workspace::PARAMETER;
			break;
		case 3:
			type = Workspace::STATE;
			break;
		default:
			ERROR_MSG("Oscilloscope::Properties::applyChannelTab : invalid type\n");
			typeList->setCurrentIndex(0);
			type = Workspace::INPUT;
	}

	struct channel_info *info;
	std::list<Scope::Channel>::iterator i = panel->getChannelsBegin();
	for (std::list<Scope::Channel>::iterator end = panel->getChannelsEnd(); i
			!= end; ++i) {
		info = reinterpret_cast<struct channel_info *> (i->getInfo());
		if (info->block == block && info->type == type && info->index
				== static_cast<size_t> (channelList->currentIndex()))
			break;
	}

	if (!activateButton->isChecked()) {
		if (i != panel->getChannelsEnd()) {
			// If triggering on this channel disable triggering
			if (i->getLabel() == panel->getTriggerChannel()->getLabel())
				panel->setTrigger(Scope::NONE, panel->getTriggerThreshold(),
						panel->getChannelsEnd(), panel->getTriggerHolding(),
						panel->getTriggerHoldoff());

			bool active = panel->setInactiveSync();
			panel->removeChannel(i);
			panel->flushFifo();
			panel->setActive(active);

			delete info;
		}
	}
	else {
		if (i == panel->getChannelsEnd()) {
			info = new struct channel_info;

			info->block = block;
			info->type = type;
			info->index = channelList->currentIndex();
			info->previous = 0.0;

			info->name = QString::number(block->getID())+" "+QString::fromStdString(block->getName(type, channelList->currentIndex()));

			bool active = panel->setInactiveSync();

			i = panel->insertChannel(info->name + " 2 V/div", 2.0, 0.0, QPen(
						Qt::red, 1, Qt::SolidLine), info);

			panel->flushFifo();
			panel->setActive(active);
		}

		double scale;
		switch (scaleList->currentIndex() % 4) {
			case 0:
				scale = pow(10, 1 - scaleList->currentIndex() / 4);
				break;
			case 1:
				scale = 5 * pow(10, -scaleList->currentIndex() / 4);
				break;
			case 2:
				scale = 2.5 * pow(10, -scaleList->currentIndex() / 4);
				break;
			case 3:
				scale = 2 * pow(10, -scaleList->currentIndex() / 4);
				break;
			default:
				ERROR_MSG("Oscilloscope::Properties::applyChannelTab : invalid scale selection\n");
				scale = 2.0;
		}
		if (scale != i->getScale()) {
			panel->setChannelScale(i, scale);
			panel->setChannelLabel(i, info->name + " "
					+ scaleList->currentText().simplified());
		}
		panel->setChannelOffset(i, offsetEdit->text().toDouble() * pow(10, -3
					* offsetList->currentIndex()));

		QPen pen;
		switch (colorList->currentIndex()) {
			case 0:
				pen.setColor(Qt::red);
				break;
			case 1:
				pen.setColor(Qt::yellow);
				break;
			case 2:
				pen.setColor(Qt::green);
				break;
			case 3:
				pen.setColor(Qt::blue);
				break;
			case 4:
				pen.setColor(Qt::magenta);
				break;
			case 5:
				pen.setColor(Qt::cyan);
				break;
			case 6:
				pen.setColor(Qt::black);
				break;
			default:
				ERROR_MSG("Oscilloscope::Properties::applyChannelTab : invalid color selection\n");
				pen.setColor(Qt::red);
		}
		pen.setWidth(widthList->currentIndex() + 1);
		switch (styleList->currentIndex()) {
			case 0:
				pen.setStyle(Qt::SolidLine);
				break;
			case 1:
				pen.setStyle(Qt::DashLine);
				break;
			case 2:
				pen.setStyle(Qt::DotLine);
				break;
			case 3:
				pen.setStyle(Qt::DashDotLine);
				break;
			case 4:
				pen.setStyle(Qt::DashDotDotLine);
				break;
			default:
				ERROR_MSG("Oscilloscope::Properties::applyChannelTab : invalid style selection\n");
				pen.setStyle(Qt::SolidLine);
		}
		panel->setChannelPen(i, pen);

		//i->label.setColor(i->getPen().color());
		//for(std::vector<QCanvasLine>::iterator j = i->lines.begin(),end = i->lines.end();j != end;++j)
		//    j->setPen(info->pen);

		/*
			 if(&*i == panel->trigChan)
			 panel->trigLine->setPoints(0,panel->val2pix(panel->trigThresh,*i),
			 width(),panel->val2pix(panel->trigThresh,*i));
			 */
	}
	showChannelTab();
}

void Oscilloscope::Properties::applyDisplayTab(void) {
	panel->setRefresh(refreshSpin->value());

	double divT;
	if (timeList->currentIndex() % 3 == 1)
		divT = 2 * pow(10, 3 - timeList->currentIndex() / 3);
	else if (timeList->currentIndex() % 3 == 2)
		divT = pow(10, 3 - timeList->currentIndex() / 3);
	else
		divT = 5 * pow(10, 3 - timeList->currentIndex() / 3);
	panel->setDivT(divT);
	panel->setPeriod(RT::System::getInstance()->getPeriod() * 1e-6);
	panel->adjustDataSize();

	Scope::trig_t trigDirection = static_cast<Scope::trig_t> (trigGroup->id(trigGroup->checkedButton()));
	double trigThreshold = trigThreshEdit->text().toDouble() * pow(10, -3 * trigThreshList->currentIndex());

	std::list<Scope::Channel>::iterator trigChannel = panel->getChannelsEnd();
	for (std::list<Scope::Channel>::iterator i = panel->getChannelsBegin(), end =
			panel->getChannelsEnd(); i != end; ++i)
		if (i->getLabel() == trigChanList->currentText()) {
			trigChannel = i;
			break;
		}
	if (trigChannel == panel->getChannelsEnd())
		trigDirection = Scope::NONE;

	bool trigHolding = trigHoldingCheck->isChecked();
	double trigHoldoff = trigHoldoffEdit->text().toDouble() * pow(10, -3
			* trigHoldoffList->currentIndex());

	panel->setTrigger(trigDirection, trigThreshold, trigChannel, trigHolding,
			trigHoldoff);

	showDisplayTab();
}

/*void Oscilloscope::Properties::createAdvancedTab(void) {

	QWidget *advancedTab = new QWidget(tabWidget);
	tabWidget->addTab(advancedTab, "Advanced");

	setWhatsThis("<p><b>Oscilloscope: Advanced Options</b><br>"
	"The Oscilloscope automatically computes the necessary buffer size based on "
	"the number of horizontal divisions displayed, the scale of time axis, "
	"and the current real-time period of the system. You may choose to "
	"downsample the oscilloscope using a sample-and-hold method.</p>");
	QBoxLayout *layout = new QVBoxLayout(advancedTab);

	QGroupBox *resBox = new QGroupBox("Data Properties", advancedTab);
	layout->addWidget(resBox);

	QBoxLayout *resLayout = new QVBoxLayout(resBox);
	resLayout->setMargin(15);

	QHBox *hbox0 = new QHBox(resBox);
	resLayout->addWidget(hbox0);
	(new QLabel("Downsampling Rate: ", hbox0))->setFixedWidth(130);
	rateSpin = new QSpinBox(1, 2, 1, hbox0);
	rateSpin->setValue(panel->downsample_rate);
	QObject::connect(rateSpin,SIGNAL(valueChanged(int)),this,SLOT(updateDownsampleRate(int)));
	rateSpin->setEnabled(true);

	QHBox *hbox1 = new QHBox(resBox);
	resLayout->addWidget(hbox1);
	(new QLabel("Data Buffer Size: ", hbox1))->setFixedWidth(130);
	sizeEdit = new QLineEdit(hbox1);
	sizeEdit->setText(QString::number(panel->getDataSize()));
	sizeEdit->setEnabled(false);

	QGroupBox *gridBox = new QGroupBox("Grid Properties", advancedTab);
	layout->addWidget(gridBox);

	QBoxLayout *gridLayout = new QVBoxLayout(gridBox);
	gridLayout->setMargin(15);

	QHBox *hbox2 = new QHBox(gridBox);
	gridLayout->addWidget(hbox2);
	(new QLabel("  X Divisions: ", hbox2))->setFixedWidth(125);
	divXSpin = new QSpinBox(hbox2);
	divXSpin->setMinValue(1);
	divXSpin->setMaxValue(25);

	QHBox *hbox3 = new QHBox(gridBox);
	gridLayout->addWidget(hbox3);
	(new QLabel("  Y Divisions: ", hbox3))->setFixedWidth(125);
	divYSpin = new QSpinBox(hbox3);
	divYSpin->setMinValue(1);
	divYSpin->setMaxValue(25);
	}*/

struct block_list_info_t {
	QComboBox *blockList;
	std::vector<IO::Block *> *blocks;
};

static void buildBlockList(IO::Block *block, void *arg) {
	block_list_info_t *info = static_cast<block_list_info_t *> (arg);
	info->blockList->addItem(QString::fromStdString(block->getName())+" "+QString::number(block->getID()));
	info->blocks->push_back(block);
}

void Oscilloscope::Properties::createChannelTab(void) {

	setWhatsThis("<p><b>Oscilloscope: Channel Options</b><br>"
			"Use the dropdown boxes to select the signal streams you want to plot from "
			"any loaded modules or your DAQ device. You may change the plotting scale for "
			"the signal, apply a DC offset, and change the color and style of the line.</p>");

	// Make parent widget and layout
	QWidget *channelTab = new QWidget;
	QBoxLayout *layout = new QVBoxLayout;

	// Create child widgets and layouts
	channelGroup = new QGroupBox(tr("Channel Selection"));
	QVBoxLayout *channelLayout = new QVBoxLayout;

	// Create elements for channel box
	channelLayout->addWidget(new QLabel(tr("Channel")));
	blockList = new QComboBox;
	channelLayout->addWidget(blockList);
	block_list_info_t info = { blockList, &panel->blocks };
	IO::Connector::getInstance()->foreachBlock(::buildBlockList, &info);
	QObject::connect(blockList,SIGNAL(activated(int)),this,SLOT(buildChannelList(void)));

	typeList = new QComboBox;
	channelLayout->addWidget(typeList);
	typeList->addItem("Input");
	typeList->addItem("Output");
	typeList->addItem("Parameter");
	typeList->addItem("State");
	QObject::connect(typeList,SIGNAL(activated(int)),this,SLOT(buildChannelList(void)));

	channelList = new QComboBox;
	QObject::connect(channelList,SIGNAL(activated(int)),this,SLOT(showTab(void)));

	// Activate button
	activateButton = new QPushButton("Active");
	channelLayout->addWidget(activateButton);
	activateButton->setCheckable(true);
	activateButton->setFixedWidth(55);
	QObject::connect(activateButton,SIGNAL(toggled(bool)),this,SLOT(activateChannel(bool)));

	channelGroup->setLayout(channelLayout);

	// Display child widget and layout
	displayGroup = new QGroupBox(tr("Display Properties"));
	QVBoxLayout *displayLayout = new QVBoxLayout;

	// Create elements for display box
	displayLayout->addWidget(new QLabel(tr("   Scale: ")));
	scaleList = new QComboBox;
	displayLayout->addWidget(scaleList);
	QFont scaleListFont("DejaVu Sans Mono");
	scaleList->setFont(scaleListFont);
	scaleList->addItem(" 10    V/div"); // 0  case 0
	scaleList->addItem("  5    V/div"); // 1  case 1
	scaleList->addItem("  2.5  V/div");	// 2  case 2
	scaleList->addItem("  2    V/div"); // 3  case 3
	scaleList->addItem("  1    V/div"); // 4  case 0
	scaleList->addItem("500   mV/div"); // 5  case 1
	scaleList->addItem("250   mV/div"); // 6  case 2
	scaleList->addItem("200   mV/div"); // 7  case 3
	scaleList->addItem("100   mV/div"); // 8  case 0
	scaleList->addItem(" 50   mV/div"); // 9  case 1
	scaleList->addItem(" 25   mV/div");
	scaleList->addItem(" 20   mV/div");
	scaleList->addItem(" 10   mV/div");
	scaleList->addItem("  5   mV/div");
	scaleList->addItem("  2.5 mV/div");
	scaleList->addItem("  2   mV/div");
	scaleList->addItem("  1   mV/div");
	QChar mu = QChar(0x3BC);
	QString suffix = QString("V/div");
	QString text = QString("500   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("250   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("200   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("100   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString(" 50   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString(" 25   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString(" 20   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString(" 10   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("  5   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("  2.5 ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("  2   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);
	text = QString("  1   ");
	text.append(mu);
	text.append(suffix);
	scaleList->addItem(text);

	scaleList->addItem("500   nV/div");
	scaleList->addItem("250   nV/div");
	scaleList->addItem("200   nV/div");
	scaleList->addItem("100   nV/div");
	scaleList->addItem(" 50   nV/div");
	scaleList->addItem(" 25   nV/div");
	scaleList->addItem(" 20   nV/div");
	scaleList->addItem(" 10   nV/div");
	scaleList->addItem("  5   nV/div");
	scaleList->addItem("  2.5 nV/div");
	scaleList->addItem("  2   nV/div");
	scaleList->addItem("  1   nV/div");
	scaleList->addItem("500   pV/div");
	scaleList->addItem("250   pV/div");
	scaleList->addItem("200   pV/div");
	scaleList->addItem("100   pV/div");
	scaleList->addItem(" 50   pV/div");
	scaleList->addItem(" 25   pV/div");
	scaleList->addItem(" 20   pV/div");
	scaleList->addItem(" 10   pV/div");
	scaleList->addItem("  5   pV/div");
	scaleList->addItem("  2.5 pV/div");
	scaleList->addItem("  2   pV/div");
	scaleList->addItem("  1   pV/div");
	scaleList->addItem("500   fV/div");
	scaleList->addItem("250   fV/div");
	scaleList->addItem("200   fV/div");
	scaleList->addItem("100   fV/div");
	scaleList->addItem(" 50   fV/div");
	scaleList->addItem(" 25   fV/div");
	scaleList->addItem(" 20   fV/div");
	scaleList->addItem(" 10   fV/div");
	scaleList->addItem("  5   fV/div");
	scaleList->addItem("  2.5 fV/div");
	scaleList->addItem("  2   fV/div");
	scaleList->addItem("  1   fV/div");

	// Offset items
	displayLayout->addWidget(new QLabel(tr("   Offset: ")));
	offsetEdit = new QLineEdit;
	offsetEdit->setValidator(new QDoubleValidator(offsetEdit));
	displayLayout->addWidget(offsetEdit);
	offsetList = new QComboBox;
	displayLayout->addWidget(offsetList);
	offsetList->addItem("V");
	offsetList->addItem("mV");
	offsetList->addItem("uV");
	offsetList->addItem("nV");
	offsetList->addItem("pV");

	displayGroup->setLayout(displayLayout);

	/*

	lineBox = new QGroupBox("Line Properties", channelTab);
	layout->addWidget(lineBox);

	QBoxLayout *lineLayout = new QVBoxLayout(lineBox);
	lineLayout->setMargin(15);

	QHBox *hbox3 = new QHBox(lineBox);
	lineLayout->addWidget(hbox3);
	(new QLabel("   Color: ", hbox3))->setFixedWidth(125);
	colorList = new QComboBox(hbox3);
	QPixmap tmp(25, 25);
	tmp.fill(Qt::red);
	colorList->addItem(tmp, " Red");
	tmp.fill(Qt::yellow);
	colorList->addItem(tmp, " Yellow");
	tmp.fill(Qt::green);
	colorList->addItem(tmp, " Green");
	tmp.fill(Qt::blue);
	colorList->addItem(tmp, " Blue");
	tmp.fill(Qt::magenta);
	colorList->addItem(tmp, " Magenta");
	tmp.fill(Qt::cyan);
	colorList->addItem(tmp, " Cyan");
	tmp.fill(Qt::black);
	colorList->addItem(tmp, " Black");

	QHBox *hbox4 = new QHBox(lineBox);
	lineLayout->addWidget(hbox4);
	(new QLabel("   Width: ", hbox4))->setFixedWidth(125);
	widthList = new QComboBox(hbox4);
	tmp.fill(Qt::white);
	QPainter painter(&tmp);
	for (int i = 1; i < 6; i++) {
		painter.setPen(QPen(Qt::black, i));
		painter.drawLine(0, 12, 25, 12);
		widthList->addItem(tmp, QString::number(i) + QString(" Pixels"));
	}

	QHBox *hbox5 = new QHBox(lineBox);
	lineLayout->addWidget(hbox5);
	(new QLabel("   Style: ", hbox5))->setFixedWidth(125);
	styleList = new QComboBox(hbox5);
	tmp.fill(Qt::white);
	painter.setPen(QPen(Qt::black, 3, Qt::SolidLine));
	painter.drawLine(0, 12, 25, 12);
	styleList->addItem(tmp, QString(" Solid"));
	tmp.fill(Qt::white);
	painter.setPen(QPen(Qt::black, 3, Qt::DashLine));
	painter.drawLine(0, 12, 25, 12);
	styleList->addItem(tmp, QString(" Dash"));
	tmp.fill(Qt::white);
	painter.setPen(QPen(Qt::black, 3, Qt::DotLine));
	painter.drawLine(0, 12, 25, 12);
	styleList->addItem(tmp, QString(" Dot"));
	tmp.fill(Qt::white);
	painter.setPen(QPen(Qt::black, 3, Qt::DashDotLine));
	painter.drawLine(0, 12, 25, 12);
	styleList->addItem(tmp, QString(" Dash Dot"));
	tmp.fill(Qt::white);
	painter.setPen(QPen(Qt::black, 3, Qt::DashDotDotLine));
	painter.drawLine(0, 12, 25, 12);
	styleList->addItem(tmp, QString(" Dash Dot Dot"));*/

	layout->addWidget(channelGroup);
	layout->addWidget(displayGroup);

	tabWidget->addTab(channelTab, tr("Channel"));
	setLayout(layout);
	buildChannelList();
}

/*void Oscilloscope::Properties::createDisplayTab(void) {

	QWidget *displayTab = new QWidget(tabWidget);
	tabWidget->addTab(displayTab, "Display");

	setWhatsThis("<p><b>Oscilloscope: Display Options</b><br>"
	"Use the dropdown box to select the time scale for the Oscilloscope. This "
	"scaling is applied to all signals plotted in the same window. You may also "
	"set a trigger on any signal that is currently plotted in the window. A yellow "
	"line will appear at the trigger threshold.</p>");

	QBoxLayout *layout = new QVBoxLayout(displayTab);

	QGroupBox *timeBox = new QGroupBox("Time Properties", displayTab);
	layout->addWidget(timeBox);

	QBoxLayout *timeLayout = new QVBoxLayout(timeBox);
	timeLayout->setMargin(15);
	QChar mu = QChar(0x3BC);
	QHBox *hbox0 = new QHBox(timeBox);
	timeLayout->addWidget(hbox0);
	(new QLabel("Time Scale:", hbox0))->setFixedWidth(125);
	timeList = new QComboBox(hbox0);
	QFont timeListFont("DejaVu Sans Mono");
	timeList->setFont(timeListFont);
	timeList->addItem("  5  s/div");
	timeList->addItem("  2  s/div");
	timeList->addItem("  1  s/div");
	timeList->addItem("500 ms/div");
	timeList->addItem("200 ms/div");
	timeList->addItem("100 ms/div");
	timeList->addItem(" 50 ms/div");
	timeList->addItem(" 20 ms/div");
	timeList->addItem(" 10 ms/div");
	timeList->addItem("  5 ms/div");
	timeList->addItem("  2 ms/div");
	timeList->addItem("  1 ms/div");
	QString suffix = QString("s/div");
	QString text = QString("500 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString("200 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString("100 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString(" 50 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString(" 20 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString(" 10 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString("  5 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString("  2 ");
	text.append(mu);
	text.append(suffix);
	timeList->addItem(text);
	text = QString("  1 ");
text.append(mu);
text.append(suffix);
timeList->addItem(text);

QHBox *hbox1 = new QHBox(timeBox);
timeLayout->addWidget(hbox1);
(new QLabel("Screen Refresh:", hbox1))->setFixedWidth(125);
refreshSpin = new QSpinBox(hbox1);
refreshSpin->setMinValue(10);
refreshSpin->setMaxValue(10000);

QGroupBox *triggerBox = new QGroupBox("Trigger Properties", displayTab);
layout->addWidget(triggerBox);

QBoxLayout *triggerLayout = new QVBoxLayout(triggerBox);
triggerLayout->setMargin(15);

QHBox *hbox2 = new QHBox(triggerBox);
triggerLayout->addWidget(hbox2);
(new QLabel("Trigger:", hbox2))->setFixedWidth(125);
trigGroup = new QHButtonGroup(hbox2);
trigGroup->setRadioButtonExclusive(true);
trigGroup->setLineWidth(0);
trigGroup->addButton(new QRadioButton("Off", trigGroup), Scope::NONE);
trigGroup->addButton(new QRadioButton("+", trigGroup), Scope::POS);
trigGroup->addButton(new QRadioButton("-", trigGroup), Scope::NEG);

QHBox *hbox3 = new QHBox(triggerBox);
triggerLayout->addWidget(hbox3);
(new QLabel("Trigger Channel:", hbox3))->setFixedWidth(125);
trigChanList = new QComboBox(hbox3);

QHBox *hbox4 = new QHBox(triggerBox);
triggerLayout->addWidget(hbox4);
(new QLabel("Trigger Threshold:", hbox4))->setFixedWidth(125);
trigThreshEdit = new QLineEdit(hbox4);
trigThreshEdit->setValidator(new QDoubleValidator(trigThreshEdit));
trigThreshList = new QComboBox(hbox4);
trigThreshList->addItem("V");
trigThreshList->addItem("mV");
trigThreshList->addItem("uV");
trigThreshList->addItem("nV");
trigThreshList->addItem("pV");

QHBox *hbox5 = new QHBox(triggerBox);
triggerLayout->addWidget(hbox5);
(new QLabel("Trigger Holding:", hbox5))->setFixedWidth(125);
trigHoldingCheck = new QCheckBox(hbox5);

QHBox *hbox6 = new QHBox(triggerBox);
triggerLayout->addWidget(hbox6);
(new QLabel("Trigger Holdoff:", hbox6))->setFixedWidth(125);
trigHoldoffEdit = new QLineEdit(hbox6);
trigHoldoffEdit->setValidator(new QDoubleValidator(trigHoldoffEdit));
trigHoldoffList = new QComboBox(hbox6);
trigHoldoffList->addItem("ms");
trigHoldoffList->addItem("us");
trigHoldoffList->addItem("ns");
}*/

void Oscilloscope::Properties::showAdvancedTab(void) {
	//rateSpin->setValue(panel->rate);
	sizeEdit->setText(QString::number(panel->getDataSize()));

	divXSpin->setValue(panel->getDivX());
	divYSpin->setValue(panel->getDivY());
}

void Oscilloscope::Properties::showChannelTab(void) {

	IO::flags_t type;
	switch (typeList->currentIndex())
	{
		case 0:
			type = Workspace::INPUT;
			break;
		case 1:
			type = Workspace::OUTPUT;
			break;
		case 2:
			type = Workspace::PARAMETER;
			break;
		case 3:
			type = Workspace::STATE;
			break;
		default:
			ERROR_MSG("Oscilloscope::Properties::showChannelTab : invalid type\n");
			typeList->setCurrentIndex(0);
			type = Workspace::INPUT;
	}

	bool found = false;

	for (std::list<Scope::Channel>::iterator i = panel->getChannelsBegin(), end = panel->getChannelsEnd(); i != end; ++i) {
		struct channel_info *info =
			reinterpret_cast<struct channel_info *> (i->getInfo());
		if (!info)
			continue;
		if (info->block && info->block == panel->blocks[blockList->currentIndex()]
				&& info->type == type && info->index == static_cast<size_t> (channelList->currentIndex())) {
			found = true;

			scaleList->setCurrentIndex(static_cast<int> (round(4 * (log10(1/i->getScale()) + 1))));

			double offset = i->getOffset();
			int offsetUnits = 0;
			if (offset)
				while (fabs(offset) < 1) {
					offset *= 1000;
					offsetUnits++;
				}
			offsetEdit->setText(QString::number(offset));
			offsetList->setCurrentIndex(offsetUnits);

			if (i->getPen().color() == Qt::red)
				colorList->setCurrentIndex(0);
			else if (i->getPen().color() == Qt::yellow)
				colorList->setCurrentIndex(1);
			else if (i->getPen().color() == Qt::green)
				colorList->setCurrentIndex(2);
			else if (i->getPen().color() == Qt::blue)
				colorList->setCurrentIndex(3);
			else if (i->getPen().color() == Qt::magenta)
				colorList->setCurrentIndex(4);
			else if (i->getPen().color() == Qt::cyan)
				colorList->setCurrentIndex(5);
			else if (i->getPen().color() == Qt::black)
				colorList->setCurrentIndex(6);
			else {
				ERROR_MSG("Oscilloscope::Properties::displayChannelTab : invalid color selection\n");
				colorList->setCurrentIndex(0);
			}

			switch (i->getPen().style()) {
				case Qt::SolidLine:
					styleList->setCurrentIndex(0);
					break;
				case Qt::DashLine:
					styleList->setCurrentIndex(1);
					break;
				case Qt::DotLine:
					styleList->setCurrentIndex(2);
					break;
				case Qt::DashDotLine:
					styleList->setCurrentIndex(3);
					break;
				case Qt::DashDotDotLine:
					styleList->setCurrentIndex(4);
					break;
				default:
					ERROR_MSG("Oscilloscope::Properties::displayChannelTab : invalid style selection\n");
					styleList->setCurrentIndex(0);
			}
			break;
		}
	}

	activateButton->setCheckable(found);
	/*displayBox->setEnabled(found);
	lineBox->setEnabled(found);
	if (!found) {
		scaleList->setCurrentIndex(3);
		offsetEdit->setText(QString::number(0));
		offsetList->setCurrentIndex(0);
		colorList->setCurrentIndex(0);
		widthList->setCurrentIndex(0);
		styleList->setCurrentIndex(0);
	}*/
}

void Oscilloscope::Properties::showDisplayTab(void) {
	timeList->setCurrentIndex(static_cast<int> (round(3 * log10(1/panel->getDivT()) + 11)));
	refreshSpin->setValue(panel->getRefresh());

	//VISITTWO
	//static_cast<QRadioButton *> (trigGroup->id(panel->getTriggerDirection()))->setChecked(true);

	//QString name;
	trigChanList->clear();
	for (std::list<Scope::Channel>::iterator i = panel->getChannelsBegin(), end =	panel->getChannelsEnd(); i != end; ++i) {
		trigChanList->addItem(i->getLabel());
		if (i == panel->getTriggerChannel())
			trigChanList->setCurrentIndex(trigChanList->count() - 1);
	}
	trigChanList->addItem("<None>");
	if (panel->getTriggerChannel() == panel->getChannelsEnd())
		trigChanList->setCurrentIndex(trigChanList->count() - 1);

	int trigThreshUnits = 0;
	double trigThresh = panel->getTriggerThreshold();
	if (trigThresh != 0.0)
		while (fabs(trigThresh) < 1) {
			trigThresh *= 1000;
			++trigThreshUnits;
		}
	trigThreshList->setCurrentIndex(trigThreshUnits);
	trigThreshEdit->setText(QString::number(trigThresh));
	trigHoldingCheck->setChecked(panel->getTriggerHolding());
	int trigHoldoffUnits = 0;
	double trigHoldoff = panel->getTriggerHoldoff();
	if (trigHoldoff != 0.0)
		while (fabs(trigHoldoff) < 1)	{
			trigHoldoff *= 1000;
			++trigHoldoffUnits;
		}
	trigHoldoffList->setCurrentIndex(trigHoldoffUnits);
	trigHoldoffEdit->setText(QString::number(trigHoldoff));
}

void Oscilloscope::Properties::updateDownsampleRate(int r) {
	downsample_rate = r;
	panel->updateDownsampleRate(downsample_rate);

}

Oscilloscope::Panel::Panel(QWidget *parent) :	Scope(parent), RT::Thread(0), fifo(10 * 1048576) {

	setAttribute(Qt::WA_DeleteOnClose);
	setWhatsThis("<p><b>Oscilloscope:</b><br>The Oscilloscope allows you to plot any signal "
			"in your workspace in real-time, including signals from your DAQ card and those "
			"generated by user modules. Multiple signals are overlaid in the window and "
			"different line colors and styles can be selected. When a signal is added, a legend "
			"automatically appears in the bottom of the window. Multiple oscilloscopes can "
			"be instantiated to give you multiple data windows. To select signals for plotting, "
			"use the right-click context \"Properties\" menu item. After selecting a signal, you must "
			"click the \"Active\" button for it to appear in the window. To change signal settings, "
			"you must click the \"Apply\" button. The right-click context \"Pause\" menu item "
			"allows you to start and stop real-time plotting.</p>");
	adjustDataSize();
	properties = new Properties(this);

	QTimer *otimer = new QTimer;
	QObject::connect(otimer,SIGNAL(timeout(void)),this,SLOT(timeoutEvent(void)));
	otimer->start(25);

	resize(800, 450);
	counter = 0;
	downsample_rate = 1;
	setActive(true);
	setWindowTitle(QString::number(getID()) + " Oscilloscope");
	show();
}

Oscilloscope::Panel::~Panel(void) {
	while (getChannelsBegin() != getChannelsEnd())
		delete reinterpret_cast<struct channel_info *> (removeChannel(
					getChannelsBegin()));

	Plugin::getInstance()->removeOscilloscopePanel(this);
	delete properties;
}

void Oscilloscope::Panel::updateDownsampleRate(int r) {
	downsample_rate = r;
}

void Oscilloscope::Panel::execute(void) {
	//void *buffer;
	size_t nchans = getChannelCount();

	if (nchans) {
		size_t idx = 0;
		size_t token = nchans;
		double data[nchans];

		if (!counter++) {
			for (std::list<Scope::Channel>::iterator i = getChannelsBegin(), end =
					getChannelsEnd(); i != end; ++i)
			{
				struct channel_info *info =
					reinterpret_cast<struct channel_info *> (i->getInfo());

				double value = info->block->getValue(info->type, info->index);

				if (i == getTriggerChannel())
				{
					double thresholdValue = getTriggerThreshold();

					if ((thresholdValue > value && thresholdValue
								< info->previous) || (thresholdValue < value
									&& thresholdValue > info->previous))
					{
						Event::Object event(Event::THRESHOLD_CROSSING_EVENT);
						int direction = (thresholdValue > value) ? 1 : -1;

						event.setParam("block", info->block);
						event.setParam("type", &info->type);
						event.setParam("index", &info->index);
						event.setParam("direction", &direction);
						event.setParam("threshold", &thresholdValue);

						Event::Manager::getInstance()->postEventRT(&event);
					}
				}
				info->previous = value; // automatically buffers a single value
				data[idx++] = value;
			}

			fifo.write(&token, sizeof(token));
			fifo.write(data, sizeof(data));
		}
		else
		{
			double prevdata[nchans];
			for (std::list<Scope::Channel>::iterator i = getChannelsBegin(), end =
					getChannelsEnd(); i != end; ++i)
			{
				struct channel_info *info =
					reinterpret_cast<struct channel_info *> (i->getInfo());
				prevdata[idx++] = info->previous;
			}
			fifo.write(&token, sizeof(token));
			fifo.write(prevdata, sizeof(prevdata));
		}
	}
	counter %= downsample_rate;
}

bool Oscilloscope::Panel::setInactiveSync(void) {
	bool active = getActive();

	setActive(false);

	SyncEvent event;
	RT::System::getInstance()->postEvent(&event);

	return active;
}

void Oscilloscope::Panel::flushFifo(void) {
	char junk;
	while (fifo.read(&junk, sizeof(junk), false))
		;
}

void Oscilloscope::Panel::adjustDataSize(void) {
	double period = RT::System::getInstance()->getPeriod() * 1e-6; // ms
	size_t size = ceil(getDivT() * getDivX() / period) + 1;
	setDataSize(size);
}

void Oscilloscope::Panel::showProperties(void) {
	properties->show();
	properties->raise();
	properties->move(mapToGlobal(rect().center()) - properties->rect().center());
}

void Oscilloscope::Panel::timeoutEvent(void) {
	size_t size;

	while (fifo.read(&size, sizeof(size), false)) {
		double data[size];
		if (fifo.read(data, sizeof(data)))
			setData(data, size);
	}
}

void Oscilloscope::Panel::mouseDoubleClickEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && getTriggerChannel() != getChannelsEnd()) {
		double scale = height() / (getTriggerChannel()->getScale() * getDivY());
		double offset = getTriggerChannel()->getOffset();
		double threshold = (height() / 2 - e->y()) / scale - offset;

		setTrigger(getTriggerDirection(), threshold, getTriggerChannel(),
				getTriggerHolding(), getTriggerHoldoff());
		properties->showDisplayTab();
	}
}

/*
 * Set up mouse events for right click press
 */
void Oscilloscope::Panel::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::RightButton) {
		QMenu menu(this);
		menu.addAction("Pause",this,SLOT(togglePause(void)),paused());//,paused()->setChecked();
		menu.addAction("Properties",this,SLOT(showProperties(void)));
		menu.addAction("Exit",this,SLOT(close(void)));
		menu.setMouseTracking(true);
		menu.exec(QCursor::pos());
	}
}

void Oscilloscope::Panel::doDeferred(const Settings::Object::State &s) {
	bool active = setInactiveSync();

	for (size_t i = 0, nchans = s.loadInteger("Num Channels"); i < nchans; ++i)	{
		std::ostringstream str;
		str << i;

		IO::Block
			*block = dynamic_cast<IO::Block *> (Settings::Manager::getInstance()->getObject(s.loadInteger(str.str() + " ID")));
		if (!block)
			continue;

		struct channel_info *info = new struct channel_info;

		info->block = block;
		info->type = s.loadInteger(str.str() + " type");
		info->index = s.loadInteger(str.str() + " index");
		info->name = QString::number(block->getID())+" "+QString::fromStdString(block->getName(info->type, info->index));
		info->previous = 0.0;

		std::list<Scope::Channel>::iterator chan =
			insertChannel(info->name, s.loadDouble(str.str() + " scale"),
					s.loadDouble(str.str() + " offset"), QPen(QColor(QString::fromStdString(s.loadString(str.str() + " pen color"))), s.loadInteger(str.str()
							+ " pen width"), static_cast<Qt::PenStyle> (s.loadInteger(str.str() + " pen style"))), info);

		setChannelLabel(chan, info->name + " " + properties->
				scaleList->itemText(static_cast<int> (round(4 * (log10(1/chan->getScale()) + 1)))).simplified());
	}

	flushFifo();
	setActive(active);
}

void Oscilloscope::Panel::doLoad(const Settings::Object::State &s) {
	setDataSize(s.loadInteger("Size"));
	setDivXY(s.loadInteger("DivX"), s.loadInteger("DivY"));
	setDivT(s.loadDouble("DivT"));

	if (s.loadInteger("Maximized"))
		showMaximized();
	else if (s.loadInteger("Minimized"))
		showMinimized();

	if (paused() != s.loadInteger("Paused"))
		togglePause();

	setRefresh(s.loadInteger("Refresh"));

	resize(s.loadInteger("W"), s.loadInteger("H"));
	parentWidget()->move(s.loadInteger("X"), s.loadInteger("Y"));
}

void Oscilloscope::Panel::doSave(Settings::Object::State &s) const {
	s.saveInteger("Size", getDataSize());
	s.saveInteger("DivX", getDivX());
	s.saveInteger("DivY", getDivY());
	s.saveDouble("DivT", getDivT());

	if (isMaximized())
		s.saveInteger("Maximized", 1);
	else if (isMinimized())
		s.saveInteger("Minimized", 1);

	s.saveInteger("Paused", paused());
	s.saveInteger("Refresh", getRefresh());

	QPoint pos = parentWidget()->pos();
	s.saveInteger("X", pos.x());
	s.saveInteger("Y", pos.y());
	s.saveInteger("W", width());
	s.saveInteger("H", height());

	s.saveInteger("Num Channels", getChannelCount());
	size_t n = 0;
	for (std::list<Channel>::const_iterator i = getChannelsBegin(), end =
			getChannelsEnd(); i != end; ++i)
	{
		std::ostringstream str;
		str << n++;

		const struct channel_info *info =
			reinterpret_cast<const struct channel_info *> (i->getInfo());

		s.saveInteger(str.str() + " ID", info->block->getID());
		s.saveInteger(str.str() + " type", info->type);
		s.saveInteger(str.str() + " index", info->index);

		s.saveDouble(str.str() + " scale", i->getScale());
		s.saveDouble(str.str() + " offset", i->getOffset());

		QPen pen = i->getPen();
		s.saveString(str.str() + " pen color", pen.color().name().toStdString());
		s.saveInteger(str.str() + " pen style", pen.style());
		s.saveInteger(str.str() + " pen width", pen.width());
	}
}

extern "C" Plugin::Object * createRTXIPlugin(void *) {
	return Oscilloscope::Plugin::getInstance();
}

Oscilloscope::Plugin::Plugin(void) {
	MainWindow::getInstance()->createSystemMenuItem("Oscilloscope",this,SLOT(createOscilloscopePanel(void)));
}

Oscilloscope::Plugin::~Plugin(void) {
	MainWindow::getInstance()->removeSystemMenuItem(menuID);
	while (panelList.size())
		delete panelList.front();
	instance = 0;
}

void Oscilloscope::Plugin::createOscilloscopePanel(void) {
	Panel *panel = new Panel(MainWindow::getInstance()->centralWidget());
	panelList.push_back(panel);
}

void Oscilloscope::Plugin::removeOscilloscopePanel(Oscilloscope::Panel *panel) {
	panelList.remove(panel);
}

void Oscilloscope::Plugin::doDeferred(const Settings::Object::State &s) {
	size_t i = 0;
	for (std::list<Panel *>::iterator j = panelList.begin(), end =
			panelList.end(); j != end; ++j)
		(*j)->deferred(s.loadState(QString::number(i++).toStdString()));
}

void Oscilloscope::Plugin::doLoad(const Settings::Object::State &s) {
	for (size_t i = 0; i < static_cast<size_t> (s.loadInteger("Num Panels")); ++i) {
		Panel *panel = new Panel(MainWindow::getInstance()->centralWidget());
		panelList.push_back(panel);
		panel->load(s.loadState(QString::number(i).toStdString()));
	}
}

void Oscilloscope::Plugin::doSave(Settings::Object::State &s) const {
	s.saveInteger("Num Panels", panelList.size());
	size_t n = 0;
	for (std::list<Panel *>::const_iterator i = panelList.begin(), end =
			panelList.end(); i != end; ++i)
		s.saveState(QString::number(n++).toStdString(), (*i)->save());
}

static Mutex mutex;
Oscilloscope::Plugin *Oscilloscope::Plugin::instance = 0;

Oscilloscope::Plugin * Oscilloscope::Plugin::getInstance(void) {
	if (instance)
		return instance;

	/*************************************************************************
	 * Seems like alot of hoops to jump through, but allocation isn't        *
	 *   thread-safe. So effort must be taken to ensure mutual exclusion.    *
	 *************************************************************************/

	Mutex::Locker lock(&::mutex);
	if (!instance)
		instance = new Plugin();

	return instance;
}
