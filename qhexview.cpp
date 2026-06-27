/*
Copyright (C) 2006 - 2013 Evan Teran
						  eteran@alum.rit.edu

Copyright (C) 2010        Hugues Bruant
						  hugues.bruant@gmail.com

This file can be used under one of two licenses.

1. The GNU Public License, version 2.0, in COPYING-gpl2
2. A BSD-Style License, in COPYING-bsd2.

The license chosen is at the discretion of the user of this software.
*/

#include "qhexview.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFontDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QScrollBar>
#include <QStringBuilder>
#include <QTextStream>
#include <QtEndian>
#include <QtGlobal>

#include <cctype>
#include <climits>
#include <cmath>
#include <memory>

namespace {

/**
 * @brief Determines if a character has a printable ascii symbol
 *
 * @param ch The character to check
 * @return True if the character is printable, false otherwise
 */
constexpr bool is_printable(uint8_t ch) {

	// if it's standard ascii use isprint/isspace, otherwise go with our observations
	if (ch < 0x80) {
		return std::isprint(ch) || std::isspace(ch);
	}

	return (ch & 0xff) >= 0xa0;
}

/**

 * @brief Convenience function used to add a checkable menu item to the context menu

 * @param menu The QMenu to which the action will be added
 * @param caption The text that will be displayed for the action
 * @param checked A boolean indicating whether the action should be initially checked
 * @param func A callable (function, lambda, etc.) that will be connected to the action's toggled signal
 * @return A pointer to the created QAction
 */
template <class Func>
QAction *add_toggle_action_to_menu(QMenu *menu, const QString &caption, bool checked, Func func) {
	auto action = new QAction(caption, menu);
	action->setCheckable(true);
	action->setChecked(checked);
	menu->addAction(action);
	QObject::connect(action, &QAction::toggled, func);
	return action;
}

}

/**
 * @brief Constructor for the QHexView class
 *
 * @param parent The parent widget for this QHexView instance. Defaults to nullptr if no parent is specified.
 */
QHexView::QHexView(QWidget *parent)
	: QAbstractScrollArea(parent) {

#if QT_POINTER_SIZE == 4
	addressSize_ = Address32;
#else
	addressSize_ = Address64;
#endif

	// default to a simple monospace font
	setFont(QFont("Monospace", 8));
	setShowAddressSeparator(true);
}

/**
 * @brief Sets whether to show the address separator in the address field
 *
 * @param value A boolean indicating whether to show the address separator.
 */
void QHexView::setShowAddressSeparator(bool value) {
	showAddressSeparator_ = value;
	updateScrollbars();
}

/**
 * @brief Formats an address into a QString representation based on the current address size and formatting options.
 *
 * @param address The address to format.
 * @return A QString representing the formatted address.
 */
QString QHexView::formatAddress(address_t address) const {

	static char buffer[32];

	switch (addressSize_) {
	case Address32: {
		const uint16_t hi = (address >> 16) & 0xffff;
		const uint16_t lo = (address & 0xffff);

		if (showAddressSeparator_) {
			qsnprintf(buffer, sizeof(buffer), "%04x:%04x", hi, lo);
		} else {
			qsnprintf(buffer, sizeof(buffer), "%04x%04x", hi, lo);
		}
	}
		return QString::fromLocal8Bit(buffer);
	case Address64: {
		const uint32_t hi = (address >> 32) & 0xffffffff;
		const uint32_t lo = (address & 0xffffffff);

		if (hideLeadingAddressZeros_) {
			if (showAddressSeparator_) {
				qsnprintf(buffer, sizeof(buffer), "%04x:%08x", (hi & 0xffff), lo);
			} else {
				qsnprintf(buffer, sizeof(buffer), "%04x%08x", (hi & 0xffff), lo);
			}
		} else {
			if (showAddressSeparator_) {
				qsnprintf(buffer, sizeof(buffer), "%08x:%08x", hi, lo);
			} else {
				qsnprintf(buffer, sizeof(buffer), "%08x%08x", hi, lo);
			}
		}
	}
		return QString::fromLocal8Bit(buffer);
	}

	return QString();
}

/**
 * @brief Repaints the viewport of the QHexView widget.
 */
void QHexView::repaint() {
	viewport()->repaint();
}

/**
 * @brief Calculates the size of the data being viewed in the QHexView widget.
 *
 * @return The size of the data being viewed.
 */
int64_t QHexView::dataSize() const {
	return data_ ? data_->size() : 0;
}

/**
 *
 * @brief Sets whether to hide leading zeros in the address display.
 *
 * @param value A boolean indicating whether to hide leading zeros.
 */
void QHexView::setHideLeadingAddressZeros(bool value) {
	hideLeadingAddressZeros_ = value;
}

/**
 * @brief Gets whether leading zeros are hidden in the address display.
 *
 * @return True if leading zeros are hidden, false otherwise.
 */
bool QHexView::hideLeadingAddressZeros() const {
	return hideLeadingAddressZeros_;
}

/**
 * @brief Overloaded version of setFont, calculates font metrics for later
 *
 * @param f The QFont to set for the QHexView widget.
 */
void QHexView::setFont(const QFont &f) {

	QFont font(f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QT_WARNING_PUSH
	QT_WARNING_DISABLE_DEPRECATED
	font.setStyleStrategy(QFont::ForceIntegerMetrics);
	QT_WARNING_POP
#else
	font.setHintingPreference(QFont::PreferFullHinting);
	font.setStyleStrategy(QFont::NoFontMerging);
#endif
	// recalculate all of our metrics/offsets
	const QFontMetrics fm(font);
	fontWidth_ = fm.horizontalAdvance('X');

	fontHeight_ = fm.height();

	updateScrollbars();

	// TODO(eteran): assert that we are using a fixed font & find out if we care?
	QAbstractScrollArea::setFont(font);
}

/**
 * @brief Creates the 'standard' context menu for the widget
 *
 * @return A pointer to the created context menu
 */
QMenu *QHexView::createStandardContextMenu() {

	auto menu = new QMenu(this);

	menu->addAction(tr("Set &Font"), this, SLOT(mnuSetFont()));
	menu->addSeparator();

	add_toggle_action_to_menu(menu, tr("Show A&ddress"), showAddress_, [this](bool value) {
		setShowAddress(value);
	});

	add_toggle_action_to_menu(menu, tr("Show &Hex"), showHex_, [this](bool value) {
		setShowHexDump(value);
	});

	add_toggle_action_to_menu(menu, tr("Show &Ascii"), showAscii_, [this](bool value) {
		setShowAsciiDump(value);
	});

	if (commentServer_) {
		add_toggle_action_to_menu(menu, tr("Show &Comments"), showComments_, [this](bool value) {
			setShowComments(value);
		});
	}

	if (userCanSetWordWidth_ || userCanSetRowWidth_) {
		menu->addSeparator();
	}

	if (userCanSetWordWidth_) {
		auto wordMenu = new QMenu(tr("Set Word Width"), menu);
		add_toggle_action_to_menu(wordMenu, tr("1 Byte"), wordWidth_ == 1, [this]() {
			setWordWidth(1);
		});

		add_toggle_action_to_menu(wordMenu, tr("2 Bytes"), wordWidth_ == 2, [this]() {
			setWordWidth(2);
		});

		add_toggle_action_to_menu(wordMenu, tr("4 Bytes"), wordWidth_ == 4, [this]() {
			setWordWidth(4);
		});

		add_toggle_action_to_menu(wordMenu, tr("8 Bytes"), wordWidth_ == 8, [this]() {
			setWordWidth(8);
		});

		menu->addMenu(wordMenu);
	}

	if (userCanSetRowWidth_) {
		auto rowMenu = new QMenu(tr("Set Row Width"), menu);
		add_toggle_action_to_menu(rowMenu, tr("1 Word"), rowWidth_ == 1, [this]() {
			setRowWidth(1);
		});

		add_toggle_action_to_menu(rowMenu, tr("2 Words"), rowWidth_ == 2, [this]() {
			setRowWidth(2);
		});

		add_toggle_action_to_menu(rowMenu, tr("4 Words"), rowWidth_ == 4, [this]() {
			setRowWidth(4);
		});

		add_toggle_action_to_menu(rowMenu, tr("8 Words"), rowWidth_ == 8, [this]() {
			setRowWidth(8);
		});

		add_toggle_action_to_menu(rowMenu, tr("16 Words"), rowWidth_ == 16, [this]() {
			setRowWidth(16);
		});

		menu->addMenu(rowMenu);
	}

	menu->addSeparator();
	menu->addAction(tr("&Copy Selection To Clipboard"), this, SLOT(mnuCopy()));
	menu->addAction(tr("&Copy Address To Clipboard"), this, SLOT(mnuAddrCopy()));
	return menu;
}

/**
 * @brief Default context menu event, simply shows standard menu
 *
 * @param event The context menu event that triggered this function
 */
void QHexView::contextMenuEvent(QContextMenuEvent *event) {
	QMenu *const menu = createStandardContextMenu();
	menu->exec(event->globalPos());
	delete menu;
}

/**
 * @brief Calculates the normalized offset based on the current scrollbar value and the number of bytes per row.
 *
 * @return The normalized offset, which is the byte offset corresponding to the current scrollbar position.
 */
int64_t QHexView::normalizedOffset() const {

	int64_t offset = static_cast<int64_t>(verticalScrollBar()->value()) * bytesPerRow();

	if (origin_ != 0) {
		if (offset > 0) {
			offset += origin_;
			offset -= bytesPerRow();
		}
	}

	return offset;
}

/**
 * @brief Copies the selected bytes from the QHexView widget to the system clipboard.
 */
void QHexView::mnuCopy() {
	if (hasSelectedText()) {

		QString s;
		QTextStream ss(&s);

		// current actual offset (in bytes)
		const int chars_per_row = bytesPerRow();
		int64_t offset          = normalizedOffset();

		const int64_t end       = std::max(selectionStart_, selectionEnd_);
		const int64_t start     = std::min(selectionStart_, selectionEnd_);
		const int64_t data_size = dataSize();

		// offset now refers to the first visible byte
		while (offset < end) {

			if ((offset + chars_per_row) > start) {

				data_->seek(offset);
				const QByteArray row_data = data_->read(chars_per_row);

				if (!row_data.isEmpty()) {
					if (showAddress_) {
						const address_t address_rva = addressOffset_ + offset;
						const QString addressBuffer = formatAddress(address_rva);
						ss << addressBuffer << '|';
					}

					if (showHex_) {
						drawHexDumpToBuffer(ss, offset, data_size, row_data);
						ss << "|";
					}

					if (showAscii_) {
						drawAsciiDumpToBuffer(ss, offset, data_size, row_data);
						ss << "|";
					}

					if (showComments_ && commentServer_) {
						drawCommentsToBuffer(ss, offset, data_size);
					}
				}

				ss << "\n";
			}
			offset += chars_per_row;
		}

		QApplication::clipboard()->setText(s);

		// TODO(eteran): do we want to trample the X11-selection too?
		QApplication::clipboard()->setText(s, QClipboard::Selection);
	}
}

/**
 * @brief Copies the starting address of the selected bytes to the system clipboard.
 */
void QHexView::mnuAddrCopy() {
	if (hasSelectedText()) {

		auto s = QString("0x%1").arg(selectedBytesAddress(), 0, 16);
		QApplication::clipboard()->setText(s);

		// TODO(eteran): do we want to trample the X11-selection too?
		QApplication::clipboard()->setText(s, QClipboard::Selection);
	}
}

/**
 * @brief Slot used to set the font of the widget based on dialog selector
 */
void QHexView::mnuSetFont() {
	setFont(QFontDialog::getFont(nullptr, font(), this));
}

/**
 * @brief Clears all data from the view
 */
void QHexView::clear() {
	data_ = nullptr;
	viewport()->update();
}

/**
 * @brief Checks if any text is currently selected in the QHexView widget.
 *
 * @return true if any text is selected
 */
bool QHexView::hasSelectedText() const {
	return !(selectionStart_ == -1 || selectionEnd_ == -1);
}

/**
 * @brief Checks if the word at the given index is in the viewable area.
 *
 * @param index The index of the word to check
 * @return true if the word is in the viewable area
 */
bool QHexView::isInViewableArea(int64_t index) const {

	const int64_t firstViewableWord = static_cast<int64_t>(verticalScrollBar()->value()) * rowWidth_;
	const int64_t viewableLines     = viewport()->height() / fontHeight_;
	const int64_t viewableWords     = viewableLines * rowWidth_;
	const int64_t lastViewableWord  = firstViewableWord + viewableWords;

	return index >= firstViewableWord && index < lastViewableWord;
}

/**
 * @brief Handles key press events for the QHexView widget, allowing for navigation and selection of bytes.
 *
 * @param event The key event that triggered this function
 */
void QHexView::keyPressEvent(QKeyEvent *event) {

	if (event == QKeySequence::SelectAll) {
		selectAll();
		viewport()->update();
	} else if (event == QKeySequence::MoveToStartOfDocument) {
		scrollTo(0);
	} else if (event == QKeySequence::MoveToEndOfDocument) {
		scrollTo(dataSize() - bytesPerRow());
	} else if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_Down) {
		int64_t offset = normalizedOffset();
		if (offset + 1 < dataSize()) {
			scrollTo(offset + 1);
		}
	} else if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_Up) {
		int64_t offset = normalizedOffset();
		if (offset > 0) {
			scrollTo(offset - 1);
		}
	} else if (event->modifiers() & Qt::ShiftModifier && hasSelectedText()) {
		// Attempting to match the highlighting behavior of common text
		// editors where highlighting to the left or up will keep the
		// first character (byte in our case) highlighted while also
		// extending back or up.
		auto dir = event->key();
		switch (dir) {
		case Qt::Key_Right:
			if (selectionStart_ == selectionEnd_) {
				selectionStart_ -= wordWidth_;
			}
			if (selectionEnd_ / wordWidth_ < dataSize()) {
				selectionEnd_ += wordWidth_;
			}
			break;
		case Qt::Key_Left:
			if ((selectionEnd_ - wordWidth_) == selectionStart_) {
				selectionStart_ += wordWidth_;
				selectionEnd_ -= wordWidth_;
			}
			if (selectionEnd_ / wordWidth_ > 0) {
				selectionEnd_ -= wordWidth_;
			}
			break;
		case Qt::Key_Down:
			selectionEnd_ += rowWidth_;
			selectionEnd_ = std::min(selectionEnd_, dataSize() * wordWidth_);
			break;
		case Qt::Key_Up:
			if ((selectionEnd_ - wordWidth_) == selectionStart_) {
				selectionStart_ += wordWidth_;
			}
			selectionEnd_ -= rowWidth_;
			if (selectionEnd_ <= 0) {
				selectionEnd_ = 0;
			}
			break;
		default:
			break;
		}
		viewport()->update();
	} else {
		QAbstractScrollArea::keyPressEvent(event);
	}
}

/**
 * @brief Calculates the x coordinate of the 3rd line in the QHexView widget, which corresponds to the left edge of the comment field.
 *
 * @return the x coordinate of the 3rd line
 */
int QHexView::line3() const {
	if (showAscii_) {
		const int elements = bytesPerRow();
		return asciiDumpLeft() + (elements * fontWidth_) + (fontWidth_ / 2);
	}

	return line2();
}

/**
 * @brief Calculates the x coordinate of the 2nd line in the QHexView widget, which corresponds to the left edge of the hex-dump field.
 *
 * @return the x coordinate of the 2nd line
 */
int QHexView::line2() const {
	if (showHex_) {
		const int elements = rowWidth_ * (charsPerWord() + 1) - 1;
		return hexDumpLeft() + (elements * fontWidth_) + (fontWidth_ / 2);
	}

	return line1();
}

/**
 * @brief Calculates the x coordinate of the 1st line in the QHexView widget, which corresponds to the left edge of the address field.
 *
 * @return the x coordinate of the 1st line
 */
int QHexView::line1() const {
	if (showAddress_) {
		const int elements = addressLength();
		return (elements * fontWidth_) + (fontWidth_ / 2);
	}

	return 0;
}

/**
 * @brief Calculates the x coordinate of the hex-dump field left edge in the QHexView widget.
 *
 * @return the x coordinate of the hex-dump field left edge
 */
int QHexView::hexDumpLeft() const {
	return line1() + (fontWidth_ / 2);
}

/**
 * @brief Calculates the x coordinate of the ascii-dump field left edge in the QHexView widget.
 * @return the x coordinate of the ascii-dump field left edge
 */
int QHexView::asciiDumpLeft() const {
	return line2() + (fontWidth_ / 2);
}

/**
 * @brief Calculates the x coordinate of the comment field left edge in the QHexView widget.
 * @return the x coordinate of the comment field left edge
 */
int QHexView::commentLeft() const {
	return line3() + (fontWidth_ / 2);
}

/**
 * @brief Calculates the number of characters each word takes up in the QHexView widget.
 *
 * @return the number of characters per word
 */
int QHexView::charsPerWord() const {
	return wordWidth_ * 2;
}

/**
 * @brief Calculates the length in characters the address will take up.
 *
 * @return the length in characters the address will take up
 */
int QHexView::addressLength() const {
	if (hideLeadingAddressZeros_ && addressSize_ == Address64) {
		const int addressLength = ((addressSize_ * CHAR_BIT) / 4) - 4;
		return addressLength + (showAddressSeparator_ ? 1 : 0);
	}

	const int addressLength = (addressSize_ * CHAR_BIT) / 4;
	return addressLength + (showAddressSeparator_ ? 1 : 0);
}

/**
 * @brief Updates the scrollbars based on the total number of lines and the number of lines currently viewable.
 */
void QHexView::updateScrollbars() {
	const int64_t sz = dataSize();
	const int bpr    = bytesPerRow();

	const int maxval = sz / bpr + ((sz % bpr) ? 1 : 0) - viewport()->height() / fontHeight_;

	verticalScrollBar()->setMaximum(std::max(0, maxval));
	horizontalScrollBar()->setMaximum(std::max(0, ((line3() - viewport()->width()) / fontWidth_)));
}

/**
 * @brief Scrolls the view to the given byte offset.
 *
 * @param offset the byte offset to scroll to
 */
void QHexView::scrollTo(address_t offset) {

	const int bpr     = bytesPerRow();
	origin_           = offset % bpr;
	address_t address = offset / bpr;

	updateScrollbars();

	if (origin_ != 0) {
		++address;
	}

	verticalScrollBar()->setValue(address);
	viewport()->update();
}

/**
 * @brief Sets if we are to display the address column
 *
 * @param show A boolean indicating whether to show the address column
 */
void QHexView::setShowAddress(bool show) {
	showAddress_ = show;
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief Sets if we are to display the hex-dump column
 *
 * @param show A boolean indicating whether to show the hex-dump column
 */
void QHexView::setShowHexDump(bool show) {
	showHex_ = show;
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief Sets if we are to display the comments column
 *
 * @param show A boolean indicating whether to show the comments column
 */
void QHexView::setShowComments(bool show) {
	showComments_ = show;
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief Sets if we are to display the ascii-dump column
 *
 * @param show A boolean indicating whether to show the ascii-dump column
 */
void QHexView::setShowAsciiDump(bool show) {
	showAscii_ = show;
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief sets the row width (units is words)
 *
 * @param rowWidth the row width to set
 */
void QHexView::setRowWidth(int rowWidth) {
	Q_ASSERT(rowWidth >= 0);
	rowWidth_ = rowWidth;
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief Sets how many bytes represent a word
 *
 * @param wordWidth the word width to set
 */
void QHexView::setWordWidth(int wordWidth) {
	Q_ASSERT(wordWidth >= 0);
	wordWidth_ = wordWidth;
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief Calculates the number of bytes per row based on the current row width and word width.
 *
 * @return the number of bytes per row
 */
int QHexView::bytesPerRow() const {
	return rowWidth_ * wordWidth_;
}

/**
 * @brief Converts pixel coordinates to a word offset.
 *
 * @param x the x-coordinate in pixels
 * @param y the y-coordinate in pixels
 * @return the word offset
 */
int64_t QHexView::pixelToWord(int x, int y) const {
	int64_t word = -1;

	switch (highlighting_) {
	case Highlighting::Data:
#if 0
		// Make pixels outside the word correspond to the nearest word, not to the right-hand one
        x -= fontWidth_ / 2;
#endif
		// the right edge of a box is kinda quirky, so we pretend there is one
		// extra character there
		x = std::clamp(x, line1(), line2() + fontWidth_);

		// the selection is in the data view portion
		x -= line1();

		// scale x/y down to character from pixels
		x = (x / fontWidth_) + (fmod(x, fontWidth_) >= (fontWidth_ / 2.0) ? 1 : 0);
		y /= fontHeight_;

		// make x relative to rendering mode of the bytes
		x /= (charsPerWord() + 1);
		break;
	case Highlighting::Ascii:
		x = std::clamp(x, asciiDumpLeft(), line3());

		// the selection is in the ascii view portion
		x -= asciiDumpLeft();

		// scale x/y down to character from pixels
		x /= fontWidth_;
		y /= fontHeight_;

		// make x relative to rendering mode of the bytes
		x /= wordWidth_;
		break;
	default:
		Q_ASSERT(0);
		break;
	}

	// starting offset in bytes
	int64_t start_offset = normalizedOffset();

	// convert byte offset to word offset, rounding up
	start_offset /= static_cast<unsigned int>(wordWidth_);

	if ((origin_ % wordWidth_) != 0) {
		start_offset += 1;
	}

	word = ((y * rowWidth_) + x) + start_offset;

	return word;
}

/**
 * @brief Updates the tooltip based on the currently selected bytes, displaying their range and values in different formats.
 */
void QHexView::updateToolTip() {
	if (selectedBytesSize() <= 0) {
		return;
	}

	auto sb               = selectedBytes();
	const address_t start = selectedBytesAddress();
	const address_t end   = selectedBytesAddress() + sb.size();

	auto data       = reinterpret_cast<uchar *>(sb.data());
	QString tooltip = QString("<p style='white-space:pre'>") // prevent word wrap
					  % QString("<b>Range: </b>") % formatAddress(start) % " - " % formatAddress(end);

	switch (sb.size()) {
	case sizeof(quint32):
		tooltip += QString("<br><b>UInt32:</b> ") % QString::number(qFromLittleEndian<quint32>(data)) % QString("<br><b>Int32:</b> ") % QString::number(qFromLittleEndian<qint32>(data));
		break;
	case sizeof(quint64):
		tooltip += QString("<br><b>UInt64:</b> ") % QString::number(qFromLittleEndian<quint64>(data)) % QString("<br><b>Int64</b> ") % QString::number(qFromLittleEndian<qint64>(data));
		break;
	}

	tooltip += "</p>";

	setToolTip(tooltip);
}

/**
 * @brief Handles mouse double-click events in the QHexView widget, allowing for selection of bytes based on the clicked position.
 *
 * @param event The mouse event that triggered this function
 */
void QHexView::mouseDoubleClickEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		const int x = event->position().x() + horizontalScrollBar()->value() * fontWidth_;
		const int y = event->position().y();
		if (x >= line1() && x < line2()) {

			highlighting_ = Highlighting::Data;

			const int64_t offset = pixelToWord(x, y);
			int64_t byte_offset  = offset * wordWidth_;
			if (origin_) {
				if (origin_ % wordWidth_) {
					byte_offset -= wordWidth_ - (origin_ % wordWidth_);
				}
			}

			selectionStart_ = byte_offset;
			selectionEnd_   = selectionStart_ + wordWidth_;
			viewport()->update();
		} else if (x < line1()) {
			highlighting_ = Highlighting::Data;

			const int64_t offset = pixelToWord(line1(), y);
			int64_t byte_offset  = offset * wordWidth_;
			if (origin_) {
				if (origin_ % wordWidth_) {
					byte_offset -= wordWidth_ - (origin_ % wordWidth_);
				}
			}

			const int chars_per_row = bytesPerRow();

			selectionStart_ = byte_offset;
			selectionEnd_   = byte_offset + chars_per_row;
			viewport()->update();
		}
	}

	updateToolTip();
}

/**
 * @brief Handles mouse press events in the QHexView widget, allowing for selection of bytes based on the clicked position.
 *
 * @param event The mouse event that triggered this function
 */
void QHexView::mousePressEvent(QMouseEvent *event) {

	if (event->button() == Qt::LeftButton) {
		const int x = event->position().x() + horizontalScrollBar()->value() * fontWidth_;
		const int y = event->position().y();

		if (x < line2()) {
			highlighting_ = Highlighting::Data;
		} else {
			highlighting_ = Highlighting::Ascii;
		}

		const int64_t offset = pixelToWord(x, y);
		int64_t byte_offset  = offset * wordWidth_;
		if (origin_) {
			if (origin_ % wordWidth_) {
				byte_offset -= wordWidth_ - (origin_ % wordWidth_);
			}
		}

		if (offset < dataSize()) {
			if (hasSelectedText() && (event->modifiers() & Qt::ShiftModifier)) {
				selectionEnd_ = byte_offset;
			} else {
				selectionStart_ = byte_offset;
				selectionEnd_   = selectionStart_ + wordWidth_;
			}
		} else {
			selectionStart_ = selectionEnd_ = -1;
		}
		viewport()->update();
	}
	if (event->button() == Qt::RightButton) {
	}

	updateToolTip();
}

/**
 * @brief Handles mouse move events in the QHexView widget, updating the selection based on the cursor position.
 *
 * @param event The mouse event that triggered this function
 */
void QHexView::mouseMoveEvent(QMouseEvent *event) {
	if (highlighting_ != Highlighting::None) {
		const int x = event->position().x() + horizontalScrollBar()->value() * fontWidth_;
		const int y = event->position().y();

		const int64_t offset = pixelToWord(x, y);

		if (selectionStart_ != -1) {
			if (offset == -1) {
				selectionEnd_ = rowWidth_;
			} else {

				int64_t byte_offset = (offset * wordWidth_);

				if (origin_) {
					if (origin_ % wordWidth_) {
						byte_offset -= wordWidth_ - (origin_ % wordWidth_);
					}
				}
				selectionEnd_ = byte_offset;
				if (selectionEnd_ == selectionStart_) {
					selectionEnd_ += wordWidth_;
				}
			}

			if (selectionEnd_ < 0) {
				selectionEnd_ = 0;
			}

			if (!isInViewableArea(selectionEnd_)) {
				ensureVisible(selectionEnd_);
			}
		}
		viewport()->update();
		updateToolTip();
	}
}

/**
 * @brief Handles mouse release events in the QHexView widget, resetting the highlighting state.
 *
 * @param event The mouse event that triggered this function
 */
void QHexView::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton) {
		highlighting_ = Highlighting::None;
	}
}

/**
 * @brief Ensures that the byte at the given index is visible in the viewport. This function is currently a placeholder and does not implement any functionality.
 *
 * @param index The index of the byte to ensure visibility for.
 */
void QHexView::ensureVisible(int64_t index) {
	Q_UNUSED(index)
}

/**
 * @brief  Sets the data source for the QHexView widget. If the provided QIODevice is sequential or has no size,
 * it reads all data into an internal buffer. Otherwise, it uses the provided QIODevice directly. It also
 * updates the address size based on the data size and resets the selection and scrollbars.
 *
 * @param d A pointer to the QIODevice that serves as the data source for the QHexView widget.
 */
void QHexView::setData(QIODevice *d) {
	if (d->isSequential() || !d->size()) {
		internalBuffer_ = std::make_unique<QBuffer>();
		internalBuffer_->setData(d->readAll());
		internalBuffer_->open(QBuffer::ReadOnly);
		data_ = internalBuffer_.get();
	} else {
		data_ = d;
	}

	if (data_->size() > Q_INT64_C(0xffffffff)) {
		addressSize_ = Address64;
	}

	deselect();
	updateScrollbars();
	viewport()->update();
}

/**
 * @brief Handles resize events for the QHexView widget, updating the scrollbars to accommodate the new size.
 *
 * @param event The resize event that triggered this function
 */
void QHexView::resizeEvent(QResizeEvent *) {
	updateScrollbars();
}

/**
 * @brief Sets the address offset for the QHexView widget, which is used to calculate the displayed addresses in the view.
 *
 * @param offset The address offset to set for the QHexView widget.
 */
void QHexView::setAddressOffset(address_t offset) {
	addressOffset_ = offset;
}

/**
 * @brief Checks if the byte at the given index is currently selected in the QHexView widget.
 *
 * @param index The index of the byte to check for selection.
 * @return true if the byte at the given index is selected, false otherwise.
 */
bool QHexView::isSelected(int64_t index) const {

	bool ret = false;
	if (index < dataSize()) {
		if (selectionStart_ != selectionEnd_) {
			if (selectionStart_ < selectionEnd_) {
				ret = (index >= selectionStart_ && index < selectionEnd_);
			} else {
				ret = (index >= selectionEnd_ && index < selectionStart_);
			}
		}
	}
	return ret;
}

/**
 * @brief Draws the comments associated with the given offset and row in the QHexView widget using the provided QPainter.
 *
 * @param painter The QPainter object used for drawing the comments.
 * @param offset The offset in the data for which to draw comments.
 * @param row The row number in the view where the comments should be drawn.
 * @param size The total size of the data being viewed (unused in this function).
 */
void QHexView::drawComments(QPainter &painter, int64_t offset, int row, int64_t size) const {

	Q_UNUSED(size)

	painter.setPen(palette().color(QPalette::Text));

	const address_t address = addressOffset_ + offset;
	const QString comment   = commentServer_->comment(address, wordWidth_);

	painter.drawText(
		commentLeft(),
		row,
		comment.length() * fontWidth_,
		fontHeight_,
		Qt::AlignTop,
		comment);
}

/**
 * @brief Draws the ASCII dump of the data associated with the given offset and row in the QHexView widget using the provided QTextStream.
 *
 * @param stream The QTextStream object used for writing the ASCII dump.
 * @param offset The offset in the data for which to draw the ASCII dump.
 * @param size The total size of the data being viewed.
 * @param row_data The QByteArray containing the data for the current row.
 */
void QHexView::drawAsciiDumpToBuffer(QTextStream &stream, int64_t offset, int64_t size, const QByteArray &row_data) const {
	// i is the byte index
	const int chars_per_row = bytesPerRow();
	for (int i = 0; i < chars_per_row; ++i) {
		const int64_t index = offset + i;
		if (index < size) {
			if (isSelected(index)) {
				const auto ch        = static_cast<uint8_t>(row_data[i]);
				const bool printable = is_printable(ch) && ch != '\f' && ch != '\t' && ch != '\r' && ch != '\n' && ch < 0x80;
				const char byteBuffer(printable ? ch : unprintableChar_);
				stream << byteBuffer;
			} else {
				stream << ' ';
			}
		} else {
			break;
		}
	}
}

/**
 * @brief Draws the comments associated with the given offset and row in the QHexView widget using the provided QTextStream.
 *
 * @param stream The QTextStream object used for writing the comments.
 * @param offset The offset in the data for which to draw comments.
 * @param size The total size of the data being viewed (unused in this function).
 */
void QHexView::drawCommentsToBuffer(QTextStream &stream, int64_t offset, int64_t size) const {
	Q_UNUSED(size)
	const address_t address = addressOffset_ + offset;
	const QString comment   = commentServer_->comment(address, wordWidth_);
	stream << comment;
}

/**
 * @brief Formats bytes in a way that's suitable for rendering in a hexdump having
 * this as a separate function serves two purposes.
 * #1 no code duplication between the buffer and QPainter versions
 * #2 this encourages NRVO of the return value more than an integrated function would
 *
 * @param row_data The QByteArray containing the data for the current row.
 * @param index The index of the byte to format.
 * @return The formatted byte as a QString.
 */
QString QHexView::formatBytes(const QByteArray &row_data, int index) const {

	char byte_buffer[32];

	static constexpr char hex_bytes[] = "000102030405060708090a0b0c0d0e0f"
										"101112131415161718191a1b1c1d1e1f"
										"202122232425262728292a2b2c2d2e2f"
										"303132333435363738393a3b3c3d3e3f"
										"404142434445464748494a4b4c4d4e4f"
										"505152535455565758595a5b5c5d5e5f"
										"606162636465666768696a6b6c6d6e6f"
										"707172737475767778797a7b7c7d7e7f"
										"808182838485868788898a8b8c8d8e8f"
										"909192939495969798999a9b9c9d9e9f"
										"a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
										"b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
										"c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
										"d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
										"e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
										"f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";

	switch (wordWidth_) {
	case 1:
		memcpy(&byte_buffer[0], &hex_bytes[(row_data[index + 0] & 0xff) * 2L], 2);
		break;
	case 2:
		memcpy(&byte_buffer[0], &hex_bytes[(row_data[index + 1] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[2], &hex_bytes[(row_data[index + 0] & 0xff) * 2L], 2);
		break;
	case 4:
		memcpy(&byte_buffer[0], &hex_bytes[(row_data[index + 3] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[2], &hex_bytes[(row_data[index + 2] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[4], &hex_bytes[(row_data[index + 1] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[6], &hex_bytes[(row_data[index + 0] & 0xff) * 2L], 2);
		break;
	case 8:
		memcpy(&byte_buffer[0], &hex_bytes[(row_data[index + 7] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[2], &hex_bytes[(row_data[index + 6] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[4], &hex_bytes[(row_data[index + 5] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[6], &hex_bytes[(row_data[index + 4] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[8], &hex_bytes[(row_data[index + 3] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[10], &hex_bytes[(row_data[index + 2] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[12], &hex_bytes[(row_data[index + 1] & 0xff) * 2L], 2);
		memcpy(&byte_buffer[14], &hex_bytes[(row_data[index + 0] & 0xff) * 2L], 2);
		break;
	}

	byte_buffer[wordWidth_ * 2L] = '\0';

	return QString::fromLatin1(byte_buffer);
}

/**
 * @brief Draws the hex dump of the data associated with the given offset and row in the QHexView widget using the provided QTextStream.
 *
 * @param stream The QTextStream object used for writing the hex dump.
 * @param offset The offset in the data for which to draw the hex dump.
 * @param size The total size of the data being viewed (unused in this function).
 * @param row_data The QByteArray containing the data for the current row.
 */
void QHexView::drawHexDumpToBuffer(QTextStream &stream, int64_t offset, int64_t size, const QByteArray &row_data) const {

	Q_UNUSED(size)

	// i is the word we are currently rendering
	for (int i = 0; i < rowWidth_; ++i) {

		// index of first byte of current 'word'
		const int64_t index = offset + (static_cast<int64_t>(i) * wordWidth_);

		// equal <=, not < because we want to test the END of the word we
		// about to render, not the start, it's allowed to end at the very last
		// byte
		if (index + wordWidth_ <= size) {
			const QString byteBuffer = formatBytes(row_data, i * wordWidth_);

			if (isSelected(index)) {
				stream << byteBuffer;
			} else {
				stream << QString(byteBuffer.length(), ' ');
			}

			if (i != (rowWidth_ - 1)) {
				stream << ' ';
			}
		} else {
			break;
		}
	}
}

/**
 * @brief Draws the hex dump of the data associated with the given offset and row in the QHexView widget using the provided QPainter.
 *
 * @param painter The QPainter object used for drawing the hex dump.
 * @param offset The offset in the data for which to draw the hex dump.
 * @param row The row in the widget for which to draw the hex dump.
 * @param size The total size of the data being viewed (unused in this function).
 * @param word_count A pointer to an integer tracking the number of words drawn.
 * @param row_data The QByteArray containing the data for the current row.
 */
void QHexView::drawHexDump(QPainter &painter, int64_t offset, int row, int64_t size, int *word_count, const QByteArray &row_data) const {
	const int hex_dump_left = hexDumpLeft();

	// i is the word we are currently rendering
	for (int64_t i = 0; i < rowWidth_; ++i) {

		// index of first byte of current 'word'
		const int64_t index = offset + (i * wordWidth_);

		// equal <=, not < because we want to test the END of the word we
		// about to render, not the start, it's allowed to end at the very last
		// byte
		if (index + wordWidth_ <= size) {

			const QString byteBuffer = formatBytes(row_data, i * wordWidth_);

			const int drawLeft  = hex_dump_left + (i * (charsPerWord() + 1) * fontWidth_);
			const int drawWidth = charsPerWord() * fontWidth_;

			if (isSelected(index)) {

				const QPalette::ColorGroup group = hasFocus() ? QPalette::Active : QPalette::Inactive;

				painter.fillRect(
					QRectF(
						drawLeft,
						row,
						drawWidth,
						fontHeight_),
					palette().color(group, QPalette::Highlight));

				// should be highlight the space between us and the next word?
				if (i != (rowWidth_ - 1)) {
					if (isSelected(index + 1)) {
						painter.fillRect(
							QRectF(
								drawLeft + drawWidth,
								row,
								fontWidth_,
								fontHeight_),
							palette().color(group, QPalette::Highlight));
					}
				}

				painter.setPen(palette().color(group, QPalette::HighlightedText));
			} else {
				painter.setPen(QPen((*word_count & 1) ? alternateWordColor_ : palette().color(QPalette::Text)));

				// implement cold zone stuff
				if (coldZoneEnd_ > addressOffset_ && static_cast<address_t>(offset) < coldZoneEnd_ - addressOffset_) {
					painter.setPen(QPen(coldZoneColor_));
				}
			}

			painter.drawText(
				drawLeft,
				row,
				byteBuffer.length() * fontWidth_,
				fontHeight_,
				Qt::AlignTop,
				byteBuffer);

			++(*word_count);
		} else {
			break;
		}
	}
}

/**
 * @brief Draws the ASCII dump of the data associated with the given offset and row in the QHexView widget using the provided QPainter.
 *
 * @param painter The QPainter object used for drawing the ASCII dump.
 * @param offset The offset in the data for which to draw the ASCII dump.
 * @param row The row in the widget for which to draw the ASCII dump.
 * @param size The total size of the data being viewed (unused in this function).
 * @param row_data The QByteArray containing the data for the current row.
 */
void QHexView::drawAsciiDump(QPainter &painter, int64_t offset, int row, int64_t size, const QByteArray &row_data) const {
	const int ascii_dump_left = asciiDumpLeft();

	// i is the byte index
	const int chars_per_row = bytesPerRow();
	for (int i = 0; i < chars_per_row; ++i) {

		const int64_t index = offset + i;

		if (index < size) {
			const char ch        = row_data[i];
			const int drawLeft   = ascii_dump_left + i * fontWidth_;
			const bool printable = is_printable(ch);

			// drawing a selected character
			if (isSelected(index)) {

				const QPalette::ColorGroup group = hasFocus() ? QPalette::Active : QPalette::Inactive;

				painter.fillRect(
					QRectF(
						drawLeft,
						row,
						fontWidth_,
						fontHeight_),
					palette().color(group, QPalette::Highlight));

				painter.setPen(palette().color(group, QPalette::HighlightedText));

			} else {
				painter.setPen(QPen(printable ? palette().color(QPalette::Text) : nonPrintableTextColor_));

				// implement cold zone stuff
				if (coldZoneEnd_ > addressOffset_ && static_cast<address_t>(offset) < coldZoneEnd_ - addressOffset_) {
					painter.setPen(QPen(coldZoneColor_));
				}
			}

			const QString byteBuffer(printable ? ch : unprintableChar_);

			painter.drawText(
				drawLeft,
				row,
				fontWidth_,
				fontHeight_,
				Qt::AlignTop,
				byteBuffer);
		} else {
			break;
		}
	}
}

/**
 * @brief Handles the paint event for the QHexView widget, rendering the address,
 * hex dump, ASCII dump, and comments based on the current state and settings of the widget.
 *
 * @param event The paint event that triggered this function
 */
void QHexView::paintEvent(QPaintEvent *event) {

	Q_UNUSED(event)
	QPainter painter(viewport());
	painter.translate(-horizontalScrollBar()->value() * fontWidth_, 0);

	int word_count = 0;

	// pixel offset of this row
	int row = 0;

	const int chars_per_row = bytesPerRow();

	// current actual offset (in bytes), we do this manually because we have the else
	// case unlike the helper function
	int64_t offset = static_cast<int64_t>(verticalScrollBar()->value()) * chars_per_row;

	if (origin_ != 0) {
		if (offset > 0) {
			offset += origin_;
			offset -= chars_per_row;
		} else {
			origin_ = 0;
			updateScrollbars();
		}
	}

	const int64_t data_size = dataSize();
	const int widget_height = height();

	while (row + fontHeight_ < widget_height && offset < data_size) {

		data_->seek(offset);
		const QByteArray row_data = data_->read(chars_per_row);

		if (!row_data.isEmpty()) {
			if (showAddress_) {
				const address_t address_rva = addressOffset_ + offset;
				const QString addressBuffer = formatAddress(address_rva);
				painter.setPen(QPen(addressColor_));

				// implement cold zone stuff
				if (coldZoneEnd_ > addressOffset_ && static_cast<address_t>(offset) < coldZoneEnd_ - addressOffset_) {
					painter.setPen(QPen(coldZoneColor_));
				}

				painter.drawText(0, row, addressBuffer.length() * fontWidth_, fontHeight_, Qt::AlignTop, addressBuffer);
			}

			if (showHex_) {
				drawHexDump(painter, offset, row, data_size, &word_count, row_data);
			}

			if (showAscii_) {
				drawAsciiDump(painter, offset, row, data_size, row_data);
			}

			if (showComments_ && commentServer_) {
				drawComments(painter, offset, row, data_size);
			}
		}

		offset += chars_per_row;
		row += fontHeight_;
	}

	painter.setPen(palette().color(hasFocus() ? QPalette::Active : QPalette::Inactive, QPalette::WindowText));

	if (showAddress_ && showLine1_) {
		const int vertline1_x = line1();
		painter.drawLine(vertline1_x, 0, vertline1_x, widget_height);
	}

	if (showHex_ && showLine2_) {
		const int vertline2_x = line2();
		painter.drawLine(vertline2_x, 0, vertline2_x, widget_height);
	}

	if (showAscii_ && showLine3_) {
		const int vertline3_x = line3();
		painter.drawLine(vertline3_x, 0, vertline3_x, widget_height);
	}
}

/**
 * @brief Selects all bytes in the QHexView widget.
 */
void QHexView::selectAll() {
	selectionStart_ = 0;
	selectionEnd_   = dataSize();
}

/**
 * @brief Deselects any selected bytes in the QHexView widget.
 */
void QHexView::deselect() {
	selectionStart_ = -1;
	selectionEnd_   = -1;
}

/**
 * @brief Returns all bytes from the data source of the QHexView widget as a QByteArray.
 *
 * @return A QByteArray containing all the bytes from the data source.
 */
QByteArray QHexView::allBytes() const {
	data_->seek(0);
	return data_->readAll();
}

/**
 * @brief Returns the currently selected bytes from the data source of the QHexView widget as a QByteArray.
 *
 * @return A QByteArray containing the currently selected bytes.
 */
QByteArray QHexView::selectedBytes() const {
	if (hasSelectedText()) {
		const int64_t s = std::min(selectionStart_, selectionEnd_);
		const int64_t e = std::max(selectionStart_, selectionEnd_);

		data_->seek(s);
		return data_->read(e - s);
	}

	return QByteArray();
}

/**
 * @brief Returns the address of the first byte in the currently selected range of bytes in the QHexView widget.
 *
 * @return The address of the first byte in the selected range, adjusted by the address offset. If no bytes are selected, returns the address corresponding to the minimum of selectionStart_ and selectionEnd_ plus the address offset.
 */
auto QHexView::selectedBytesAddress() const -> address_t {
	const address_t select_base = std::min(selectionStart_, selectionEnd_);
	return select_base + addressOffset_;
}

/**
 * @brief Returns the size of the currently selected range of bytes in the QHexView widget.
 *
 * @return The size of the selected range of bytes. If no bytes are selected, returns 0.
 */
uint64_t QHexView::selectedBytesSize() const {

	int64_t ret;
	if (selectionEnd_ > selectionStart_) {
		ret = selectionEnd_ - selectionStart_;
	} else {
		ret = selectionStart_ - selectionEnd_;
	}

	return ret;
}

/**
 * @brief Returns the address offset of the QHexView widget.
 *
 * @return The address offset.
 */
auto QHexView::addressOffset() const -> address_t {
	return addressOffset_;
}

/**
 * @brief Returns whether the hex dump is currently being displayed in the QHexView widget.
 *
 * @return true if the hex dump is being displayed, false otherwise.
 */
bool QHexView::showHexDump() const {
	return showHex_;
}

/**
 * @brief Returns whether the address column is currently being displayed in the QHexView widget.
 *
 * @return true if the address column is being displayed, false otherwise.
 */
bool QHexView::showAddress() const {
	return showAddress_;
}

/**
 * @brief Returns whether the ASCII dump is currently being displayed in the QHexView widget.
 *
 * @return true if the ASCII dump is being displayed, false otherwise.
 */
bool QHexView::showAsciiDump() const {
	return showAscii_;
}

/**
 * @brief Returns whether comments are currently being displayed in the QHexView widget.
 *
 * @return true if comments are being displayed, false otherwise.
 */
bool QHexView::showComments() const {
	return showComments_;
}

/**
 * @brief Returns the width of each word in the QHexView widget.
 *
 * @return The width of each word.
 */
int QHexView::wordWidth() const {
	return wordWidth_;
}

/**
 * @brief Returns the number of words displayed in each row of the QHexView widget.
 *
 * @return The number of words displayed in each row.
 */
int QHexView::rowWidth() const {
	return rowWidth_;
}

/**
 * @brief Returns the address of the first visible byte in the QHexView widget.
 *
 * @return The address of the first visible byte.
 */
auto QHexView::firstVisibleAddress() const -> address_t {
	// current actual offset (in bytes)
	int64_t offset = normalizedOffset();
	return offset + addressOffset();
}

/**
 * @brief Sets the address size for the QHexView widget, which determines how many bytes are used to represent addresses in the view.
 *
 * @param address_size The size of the address.
 */
void QHexView::setAddressSize(AddressSize address_size) {
	addressSize_ = address_size;
	viewport()->update();
}

/**
 * @brief Returns the current address size for the QHexView widget, which determines how many bytes are used to represent addresses in the view.
 *
 * @return The current address size.
 */
QHexView::AddressSize QHexView::addressSize() const {
	return addressSize_;
}

/**
 * @brief Sets the end of the cold zone in the QHexView widget, which is a region of the view that is visually distinguished from the rest of the data.
 *
 * @param offset The offset of the end of the cold zone.
 */
void QHexView::setColdZoneEnd(address_t offset) {
	coldZoneEnd_ = offset;
}

/**
 * @brief Returns whether the user is allowed to configure the word width in the QHexView widget.
 *
 * @return true if the user can configure the word width, false otherwise.
 */
bool QHexView::userConfigWordWidth() const {
	return userCanSetWordWidth_;
}

/**
 * @brief Returns whether the user is allowed to configure the row width in the QHexView widget.
 *
 * @return true if the user can configure the row width, false otherwise.
 */
bool QHexView::userConfigRowWidth() const {
	return userCanSetRowWidth_;
}

/**
 * @brief Sets whether the user is allowed to configure the word width in the QHexView widget.
 *
 * @param value true to allow user configuration of word width, false to disallow it.
 */
void QHexView::setUserConfigWordWidth(bool value) {
	userCanSetWordWidth_ = value;
	viewport()->update();
}

/**
 * @brief Sets whether the user is allowed to configure the row width in the QHexView widget.
 *
 * @param value true to allow user configuration of row width, false to disallow it.
 */
void QHexView::setUserConfigRowWidth(bool value) {
	userCanSetRowWidth_ = value;
	viewport()->update();
}

/**
 * @brief Returns the color used to display addresses in the QHexView widget.
 *
 * @return The color used for addresses.
 */
QColor QHexView::addressColor() const {
	return addressColor_;
}

/**
 * @brief Returns the color used to display the cold zone in the QHexView widget.
 *
 * @return The color used for the cold zone.
 */
QColor QHexView::coldZoneColor() const {
	return coldZoneColor_;
}

/**
 * @brief Returns the color used to display alternate words in the QHexView widget.
 *
 * @return The color used for alternate words.
 */
QColor QHexView::alternateWordColor() const {
	return alternateWordColor_;
}

/**
 * @brief Returns the color used to display non-printable text in the QHexView widget.
 *
 * @return The color used for non-printable text.
 */
QColor QHexView::nonPrintableTextColor() const {
	return nonPrintableTextColor_;
}

/**
 * @brief Sets the color used to display the cold zone in the QHexView widget.
 *
 * @param color The color to set for the cold zone.
 */
void QHexView::setColdZoneColor(const QColor &color) {
	coldZoneColor_ = color;
}

/**
 * @brief Sets the color used to display addresses in the QHexView widget.
 *
 * @param color The color to set for addresses.
 */
void QHexView::setAddressColor(const QColor &color) {
	addressColor_ = color;
}

/**
 * @brief Sets the color used to display alternate words in the QHexView widget.
 *
 * @param color The color to set for alternate words.
 */
void QHexView::setAlternateWordColor(const QColor &color) {
	alternateWordColor_ = color;
}

/**
 * @brief Sets the color used to display non-printable text in the QHexView widget.
 *
 * @param color The color to set for non-printable text.
 */
void QHexView::setNonPrintableTextColor(const QColor &color) {
	nonPrintableTextColor_ = color;
}
