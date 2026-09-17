#include "SignalChainComponent.h"
#include "ModernLookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

// A single rack-mounted module strip: full rack width, fixed "1U-ish"
// height, styled like a real stage rack's front panel - brushed-metal
// gradient fascia, corner mounting-bolt details, an etched order number
// and name, a lit power/bypass LED, and a live segmented VU meter driven
// by the pedal's own Pedal::meterLevel (see SignalChainComponent's
// generic per-pedal metering in processBlock - no per-pedal plumbing
// needed for this to work for every existing Pedal subclass). Click
// anywhere on the strip except its small buttons to open the full
// PedalInspectorWindow for this pedal's knobs. Keeping this small and
// static (nothing here changes when some OTHER pedal is added/removed
// elsewhere in the rack) is what keeps the rack from needing to destroy
// and rebuild every strip - and reset the whole rack's scroll position -
// every single time anything changes.
class SignalChainComponent::BoardTile : public juce::Component
{
public:
    BoardTile(Pedal& pedalToShow, int orderNumber,
               std::function<void()> onOpenInspector,
               std::function<void()> onRemove,
               std::function<void()> onMoveEarlier,
               std::function<void()> onMoveLater,
               bool canMoveEarlier, bool canMoveLater)
        : pedal(pedalToShow), openInspectorCallback(std::move(onOpenInspector)), removeCallback(onRemove)
    {
        orderLabel.setText(juce::String(orderNumber), juce::dontSendNotification);
        orderLabel.setFont(juce::Font(15.0f, juce::Font::bold));
        orderLabel.setJustificationType(juce::Justification::centred);
        orderLabel.setColour(juce::Label::textColourId, ModernColours::textSecondary);
        orderLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(orderLabel);

        nameLabel.setText(pedal.getName().toUpperCase(), juce::dontSendNotification);
        nameLabel.setFont(juce::Font(15.0f, juce::Font::bold));
        nameLabel.setJustificationType(juce::Justification::centredLeft);
        nameLabel.setMinimumHorizontalScale(0.7f);
        nameLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(nameLabel);

        removeButton.setButtonText("x");
        removeButton.onClick = std::move(onRemove);
        addAndMakeVisible(removeButton);

        bool engaged = ! pedal.bypassed.load();
        bypassButton.setClickingTogglesState(true);
        bypassButton.setToggleState(engaged, juce::dontSendNotification);
        bypassButton.setButtonText(engaged ? "ON" : "OFF");
        bypassButton.setColour(juce::TextButton::buttonOnColourId, ModernColours::accent);
        bypassButton.setColour(juce::TextButton::buttonColourId, ModernColours::danger);
        bypassButton.onClick = [this]
        {
            bool nowEngaged = bypassButton.getToggleState();
            pedal.bypassed.store(! nowEngaged);
            bypassButton.setButtonText(nowEngaged ? "ON" : "OFF");
            repaint();
        };
        addAndMakeVisible(bypassButton);

        moveEarlierButton.setButtonText(juce::CharPointer_UTF8("\xe2\x96\xb2")); // up-pointing triangle
        moveEarlierButton.onClick = std::move(onMoveEarlier);
        moveEarlierButton.setEnabled(canMoveEarlier);
        addAndMakeVisible(moveEarlierButton);

        moveLaterButton.setButtonText(juce::CharPointer_UTF8("\xe2\x96\xbc")); // down-pointing triangle
        moveLaterButton.onClick = std::move(onMoveLater);
        moveLaterButton.setEnabled(canMoveLater);
        addAndMakeVisible(moveLaterButton);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Brushed-metal fascia: a subtle vertical gradient rather than a
        // flat fill, catching a "studio light" highlight along the top
        // edge the way a real anodized rack panel would.
        auto engaged = ! pedal.bypassed.load();
        juce::ColourGradient fascia(ModernColours::surfaceLight, bounds.getX(), bounds.getY(),
                                     ModernColours::surface, bounds.getX(), bounds.getBottom(), false);
        fascia.addColour(0.08, ModernColours::surface.brighter(engaged ? 0.12f : 0.02f));
        g.setGradientFill(fascia);
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(engaged ? ModernColours::accentDim : ModernColours::border);
        g.drawRoundedRectangle(bounds.reduced(0.75f), 6.0f, 1.5f);

        // Rack-mount corner bolts - purely decorative, but the single
        // clearest visual cue that this is a rack panel and not just
        // another flat card.
        auto drawBolt = [&g](juce::Point<float> centre)
        {
            g.setColour(ModernColours::background);
            g.fillEllipse(centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);
            g.setColour(ModernColours::border);
            g.drawEllipse(centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f, 1.0f);
        };
        constexpr float boltInset = 10.0f;
        drawBolt({ bounds.getX() + boltInset, bounds.getY() + boltInset });
        drawBolt({ bounds.getX() + boltInset, bounds.getBottom() - boltInset });
        drawBolt({ bounds.getRight() - boltInset, bounds.getY() + boltInset });
        drawBolt({ bounds.getRight() - boltInset, bounds.getBottom() - boltInset });

        // Power/bypass LED - a soft glow when engaged, matching a real
        // rack unit's own status light.
        auto ledBounds = ledArea.toFloat();
        auto ledColour = engaged ? ModernColours::accent : ModernColours::danger.withAlpha(0.5f);
        if (engaged)
        {
            juce::ColourGradient glow(ledColour.withAlpha(0.55f), ledBounds.getCentreX(), ledBounds.getCentreY(),
                                       ledColour.withAlpha(0.0f), ledBounds.getX() - 4.0f, ledBounds.getY(), true);
            g.setGradientFill(glow);
            g.fillEllipse(ledBounds.expanded(5.0f));
        }
        g.setColour(ledColour);
        g.fillEllipse(ledBounds);
        g.setColour(ModernColours::background.withAlpha(0.6f));
        g.drawEllipse(ledBounds, 1.0f);

        // Segmented VU meter, live from the pedal's own output level -
        // 12 LED-style segments, green through amber to red like a real
        // hardware meter ladder.
        constexpr int numSegments = 12;
        auto level = pedal.meterLevel.load(std::memory_order_relaxed);
        auto levelDb = juce::Decibels::gainToDecibels(level, -48.0f);
        auto litFraction = juce::jlimit(0.0f, 1.0f, (levelDb + 42.0f) / 42.0f); // -42dB..0dB mapped to 0..1
        auto litSegments = engaged ? static_cast<int>(litFraction * numSegments + 0.5f) : 0;

        auto segArea = vuMeterArea.toFloat();
        auto segGap = 2.0f;
        auto segWidth = (segArea.getWidth() - segGap * (numSegments - 1)) / static_cast<float>(numSegments);

        for (int i = 0; i < numSegments; ++i)
        {
            auto segBounds = segArea.withX(segArea.getX() + i * (segWidth + segGap)).withWidth(segWidth);

            juce::Colour segColour = i < numSegments - 3 ? ModernColours::accent
                                    : i < numSegments - 1 ? juce::Colour(0xffe8c547)
                                                           : ModernColours::danger;

            g.setColour(i < litSegments ? segColour : ModernColours::background.withAlpha(0.7f));
            g.fillRoundedRectangle(segBounds, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        auto controlsArea = area.removeFromRight(28);
        moveEarlierButton.setBounds(controlsArea.removeFromTop(24));
        moveLaterButton.setBounds(controlsArea.removeFromBottom(24));
        removeButton.setBounds(controlsArea.reduced(0, (controlsArea.getHeight() - 24) / 2));

        area.removeFromRight(8);

        auto boltGutter = 16;
        area.removeFromLeft(boltGutter);
        area.removeFromRight(boltGutter);

        orderLabel.setBounds(area.removeFromLeft(22));
        area.removeFromLeft(6);

        auto ledColumn = area.removeFromLeft(18);
        ledArea = ledColumn.withSizeKeepingCentre(12, 12);

        area.removeFromLeft(8);

        auto bypassArea = area.removeFromRight(56);
        area.removeFromRight(10);

        nameLabel.setBounds(area.removeFromTop(area.getHeight() / 2));
        vuMeterArea = area.reduced(0, 4);

        bypassButton.setBounds(bypassArea.withSizeKeepingCentre(bypassArea.getWidth(), 24));
    }

    // Drag-to-reorder: only fires from a mouseDown on the strip's own
    // background (its buttons are separate child Components that consume
    // their own clicks first, so dragging never starts from one of
    // those). Left as an addition alongside the </> move buttons above,
    // not a replacement - a mouse drag gesture is nice to have but the
    // buttons stay the reliable fallback.
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showContextMenu();
            return;
        }

        if (onDragStart)
            onDragStart(*this, e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        if (onDragMove)
            onDragMove(*this, e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;

        bool wasDragged = e.mouseWasDraggedSinceMouseDown();

        if (onDragEnd)
            onDragEnd(*this, e);

        if (! wasDragged)
            openInspectorCallback();
    }

    // Right-click alternative to the small "x" button - a bigger,
    // easier-to-hit target for removing a unit from the rack.
    // PopupMenu::showMenuAsync's own callback already runs from the
    // message loop rather than synchronously from this mouseDown, so
    // calling removeCallback (which ultimately destroys this very tile)
    // from it is safe without extra deferral - same reasoning as
    // Content::endDrag needing callAsync but a juce::Button's onClick
    // not needing it: both patterns run the destructive call outside the
    // originating input-event's own call stack.
    void showContextMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Remove from Rack");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                            [this](int result)
        {
            if (result == 1 && removeCallback)
                removeCallback();
        });
    }

    Pedal& getPedal() const noexcept { return pedal; }

    // Set by Content right after construction, so it can coordinate the
    // drag across the whole rack (which row things land in depends on
    // every OTHER tile's position too, not just this one).
    std::function<void(BoardTile&, const juce::MouseEvent&)> onDragStart, onDragMove, onDragEnd;

private:
    Pedal& pedal;
    std::function<void()> openInspectorCallback;
    std::function<void()> removeCallback;

    juce::Label orderLabel, nameLabel;
    juce::TextButton removeButton, bypassButton, moveEarlierButton, moveLaterButton;
    juce::Rectangle<int> ledArea, vuMeterArea;
};

// The rack surface: a single vertical stack of full-width rack-unit
// strips, top to bottom, like a real physical stage rack - inside a
// vertically-scrolling Viewport so the rack can grow to any number of
// units without running off the window. A Timer keeps the VU meters
// animated even when nothing else about the chain has changed.
class SignalChainComponent::Content : public juce::Component,
                                       private juce::Timer
{
public:
    Content()
    {
        startTimerHz(30);
    }

    void rebuildTiles(const std::vector<Pedal*>& pedals,
                       const std::function<void(Pedal*)>& onOpenInspector,
                       const std::function<void(Pedal*)>& onRemove,
                       const std::function<void(Pedal*)>& onMoveEarlier,
                       const std::function<void(Pedal*)>& onMoveLater,
                       std::function<void(const std::vector<Pedal*>&)> onReorder)
    {
        draggedTile = nullptr;
        reorderCallback = std::move(onReorder);
        tiles.clear();
        displayOrder.clear();

        for (int i = 0; i < static_cast<int>(pedals.size()); ++i)
        {
            auto* pedal = pedals[static_cast<size_t>(i)];
            auto* tile = tiles.add(new BoardTile(
                *pedal, i + 1,
                [onOpenInspector, pedal] { onOpenInspector(pedal); },
                [onRemove, pedal] { onRemove(pedal); },
                [onMoveEarlier, pedal] { onMoveEarlier(pedal); },
                [onMoveLater, pedal] { onMoveLater(pedal); },
                i > 0, i < static_cast<int>(pedals.size()) - 1));

            tile->onDragStart = [this](BoardTile& t, const juce::MouseEvent& e) { beginDrag(t, e); };
            tile->onDragMove = [this](BoardTile& t, const juce::MouseEvent& e) { dragMove(t, e); };
            tile->onDragEnd = [this](BoardTile& t, const juce::MouseEvent& e) { endDrag(t, e); };

            addAndMakeVisible(tile);
            displayOrder.add(tile);
        }
    }

    bool isEmpty() const noexcept { return tiles.isEmpty(); }

    int getRequiredHeight(int /*width*/) const noexcept
    {
        if (tiles.isEmpty())
            return 0;

        return tiles.size() * (unitHeight + gap) - gap + margin * 2;
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(margin);
        layoutFromDisplayOrder(area.getX(), area.getY(), area.getWidth());
    }

private:
    void timerCallback() override { repaint(); }

    // Positions every tile at its slot in displayOrder, except the one
    // currently being dragged (that one is positioned directly by mouse Y
    // in dragMove instead) - called both from resized() (no drag active,
    // displayOrder is just creation order, so this is the ordinary
    // layout) and from dragMove (as tiles shift to open a gap).
    void layoutFromDisplayOrder(int x, int startY, int width)
    {
        int y = startY;
        for (auto* tile : displayOrder)
        {
            if (tile != draggedTile)
                tile->setBounds(x, y, width, unitHeight);
            y += unitHeight + gap;
        }
    }

    void beginDrag(BoardTile& tile, const juce::MouseEvent& e)
    {
        draggedTile = &tile;
        dragStartMouseY = e.getEventRelativeTo(this).position.y;
        dragStartTileY = static_cast<float>(tile.getY());
        tile.toFront(false);
    }

    void dragMove(BoardTile& tile, const juce::MouseEvent& e)
    {
        if (draggedTile != &tile)
            return;

        auto currentMouseY = e.getEventRelativeTo(this).position.y;
        auto newY = dragStartTileY + (currentMouseY - dragStartMouseY);
        tile.setTopLeftPosition(tile.getX(), juce::roundToInt(newY));

        auto area = getLocalBounds().reduced(margin);
        auto centreY = newY + unitHeight * 0.5f;
        auto targetRow = juce::jlimit(0, displayOrder.size() - 1,
                                       juce::roundToInt((centreY - static_cast<float>(area.getY()))
                                                         / static_cast<float>(unitHeight + gap)));

        auto currentRow = displayOrder.indexOf(&tile);
        if (currentRow >= 0 && targetRow != currentRow)
        {
            displayOrder.remove(currentRow);
            displayOrder.insert(targetRow, &tile);
            layoutFromDisplayOrder(area.getX(), area.getY(), area.getWidth());
        }
    }

    void endDrag(BoardTile& tile, const juce::MouseEvent& e)
    {
        if (draggedTile != &tile)
            return;

        draggedTile = nullptr;

        // A plain click (no actual drag) - nothing to commit. Bailing out
        // here also matters for safety, not just avoiding pointless work:
        // committing rebuilds every tile (see below), and this handler is
        // still running from inside `tile`'s own mouseUp on the call
        // stack - destroying it before that unwinds would be a
        // use-after-free.
        if (! e.mouseWasDraggedSinceMouseDown())
            return;

        std::vector<Pedal*> newOrder;
        newOrder.reserve(static_cast<size_t>(displayOrder.size()));
        for (auto* t : displayOrder)
            newOrder.push_back(&t->getPedal());

        if (reorderCallback)
        {
            // Deferred to the next message-loop iteration for the same
            // use-after-free reason as above: the callback ultimately
            // rebuilds (destroys and recreates) every tile, including
            // `tile` itself, whose mouseUp is still on the call stack.
            auto callback = reorderCallback;
            juce::MessageManager::callAsync([callback, newOrder] { callback(newOrder); });
        }
    }

    BoardTile* draggedTile = nullptr;
    float dragStartMouseY = 0.0f;
    float dragStartTileY = 0.0f;
    juce::Array<BoardTile*> displayOrder;
    std::function<void(const std::vector<Pedal*>&)> reorderCallback;

    static constexpr int unitHeight = 64;
    static constexpr int gap = 6;
    static constexpr int margin = 10;

    juce::OwnedArray<BoardTile> tiles;
};

SignalChainComponent::SignalChainComponent()
    : content(std::make_unique<Content>())
{
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false); // vertical only - the board wraps horizontally on its own
    addAndMakeVisible(viewport);
}

SignalChainComponent::~SignalChainComponent() = default;

void SignalChainComponent::setAudioConfig(double sampleRate, int maximumBlockSize, int numChannels)
{
    currentSampleRate = sampleRate;
    currentBlockSize = maximumBlockSize;
    currentNumChannels = numChannels;

    // Re-prepare every pedal already in the chain, not just ones added
    // from here on - this fires whenever the audio device changes (e.g.
    // switching to ASIO, which commonly reports a different buffer size
    // than the previous device). Without this, an existing pedal keeps
    // internal buffers sized for the old block size while process() starts
    // feeding it audio at the new one.
    const juce::ScopedLock lock(chainLock);
    for (auto& pedal : chain)
        pedal->prepare(currentSampleRate, currentBlockSize, currentNumChannels);
}

void SignalChainComponent::addPedal(std::unique_ptr<Pedal> pedal)
{
    pedal->prepare(currentSampleRate, currentBlockSize, currentNumChannels);

    {
        const juce::ScopedLock lock(chainLock);
        chain.push_back(std::move(pedal));
    }

    rebuildSlotComponents();
}

void SignalChainComponent::removePedal(Pedal* pedalToRemove)
{
    // Must close before the pedal is actually destroyed below, or the
    // inspector would be left holding a dangling Pedal&.
    closeInspector(pedalToRemove);

    {
        const juce::ScopedLock lock(chainLock);
        chain.erase(std::remove_if(chain.begin(), chain.end(),
                                    [pedalToRemove](const std::unique_ptr<Pedal>& p)
                                    { return p.get() == pedalToRemove; }),
                    chain.end());
    }

    rebuildSlotComponents();
}

void SignalChainComponent::clear()
{
    openInspectors.clear(); // close every inspector before its pedal goes away

    {
        const juce::ScopedLock lock(chainLock);
        chain.clear();
    }

    rebuildSlotComponents();
}

std::vector<Pedal*> SignalChainComponent::getPedalsInOrder() const
{
    const juce::ScopedLock lock(chainLock);

    std::vector<Pedal*> result;
    result.reserve(chain.size());
    for (auto& pedal : chain)
        result.push_back(pedal.get());

    return result;
}

void SignalChainComponent::openInspector(Pedal* pedalToInspect)
{
    for (auto& inspector : openInspectors)
    {
        if (&inspector->getPedal() == pedalToInspect)
        {
            inspector->toFront(true);
            return;
        }
    }

    openInspectors.push_back(std::make_unique<PedalInspectorWindow>(
        *pedalToInspect, [this, pedalToInspect] { closeInspector(pedalToInspect); },
        [this, pedalToInspect] { removePedal(pedalToInspect); }));
}

void SignalChainComponent::closeInspector(Pedal* pedalToClose)
{
    openInspectors.erase(std::remove_if(openInspectors.begin(), openInspectors.end(),
                                         [pedalToClose](const std::unique_ptr<PedalInspectorWindow>& w)
                                         { return &w->getPedal() == pedalToClose; }),
                          openInspectors.end());
}

void SignalChainComponent::moveExistingPedal(Pedal* pedalToMove, int delta)
{
    {
        const juce::ScopedLock lock(chainLock);
        auto it = std::find_if(chain.begin(), chain.end(),
                                [pedalToMove](const std::unique_ptr<Pedal>& p) { return p.get() == pedalToMove; });
        if (it == chain.end())
            return;

        auto index = std::distance(chain.begin(), it);
        auto newIndex = index + delta;
        if (newIndex < 0 || newIndex >= static_cast<std::ptrdiff_t>(chain.size()))
            return;

        std::swap(chain[static_cast<size_t>(index)], chain[static_cast<size_t>(newIndex)]);
    }

    rebuildSlotComponents();
}

void SignalChainComponent::reorderPedals(const std::vector<Pedal*>& newOrder)
{
    {
        const juce::ScopedLock lock(chainLock);
        std::vector<std::unique_ptr<Pedal>> reordered;
        reordered.reserve(chain.size());

        for (auto* wanted : newOrder)
        {
            auto it = std::find_if(chain.begin(), chain.end(),
                                    [wanted](const std::unique_ptr<Pedal>& p) { return p.get() == wanted; });
            if (it != chain.end())
                reordered.push_back(std::move(*it));
        }

        for (auto& leftover : chain)
            if (leftover != nullptr)
                reordered.push_back(std::move(leftover));

        chain = std::move(reordered);
    }

    rebuildSlotComponents();
}

void SignalChainComponent::rebuildSlotComponents()
{
    content->rebuildTiles(getPedalsInOrder(),
                           [this](Pedal* p) { openInspector(p); },
                           [this](Pedal* p) { removePedal(p); },
                           [this](Pedal* p) { moveExistingPedal(p, -1); },
                           [this](Pedal* p) { moveExistingPedal(p, 1); },
                           [this](const std::vector<Pedal*>& newOrder) { reorderPedals(newOrder); });

    resized();
    repaint();
}

void SignalChainComponent::processBlock(float* const* channelData, int numChannels, int numSamples)
{
    const juce::ScopedTryLock lock(chainLock);
    if (!lock.isLocked())
        return;

    for (auto& pedal : chain)
    {
        if (pedal->bypassed.load(std::memory_order_relaxed))
        {
            pedal->meterLevel.store(0.0f, std::memory_order_relaxed);
            continue;
        }

        pedal->process(channelData, numChannels, numSamples);

        // Generic peak meter, measured here rather than inside each
        // pedal's own process() - see Pedal::meterLevel - so the rack
        // UI's VU meters reflect every pedal's real output level with no
        // per-pedal plumbing required.
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            for (int n = 0; n < numSamples; ++n)
                peak = std::max(peak, std::abs(channelData[ch][n]));
        pedal->meterLevel.store(peak, std::memory_order_relaxed);
    }
}

void SignalChainComponent::paint(juce::Graphics& g)
{
    // The rack frame itself - a darker recessed well with vertical
    // mounting rails down each edge, like the fixed frame a real rack's
    // units screw into (drawn once here, behind the scrolling units).
    auto bounds = getLocalBounds();
    g.setColour(ModernColours::background.darker(0.2f));
    g.fillRect(bounds);

    g.setColour(ModernColours::surfaceLight);
    g.fillRect(bounds.removeFromLeft(railWidth));
    g.fillRect(bounds.removeFromRight(railWidth));

    g.setColour(ModernColours::border);
    g.drawRect(getLocalBounds(), 1);

    if (content->isEmpty())
    {
        g.setColour(ModernColours::textSecondary);
        g.drawFittedText("RACK EMPTY\nAdd modules from the tabs to fill it up",
                          getLocalBounds().reduced(20), juce::Justification::centred, 2);
    }
}

void SignalChainComponent::resized()
{
    // Inset by the rack frame's own mounting-rail width (drawn in
    // paint()) so the rails stay visible outside the scrolling units
    // instead of being covered by the viewport.
    viewport.setBounds(getLocalBounds().reduced(railWidth, 0));

    auto width = juce::jmax(1, viewport.getWidth() - viewport.getScrollBarThickness());
    auto height = juce::jmax(viewport.getHeight(), content->getRequiredHeight(width));
    content->setSize(width, height);

    // setSize() only calls resized() (which is what actually positions the
    // tiles) if the size actually changed - and it often doesn't, since an
    // empty/lightly-populated board stays pinned to the viewport's own
    // height (the jmax fallback above) across many add/remove calls. Call
    // it directly so new tiles are always laid out, not just on the
    // comparatively rare occasions the content's overall size changes.
    content->resized();
}
