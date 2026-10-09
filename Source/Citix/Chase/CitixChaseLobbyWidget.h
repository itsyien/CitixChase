#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Network/CitixSessionSubsystem.h"
#include "CitixChaseLobbyWidget.generated.h"
class UButton; class UEditableTextBox; class UTextBlock; class UVerticalBox; class UScrollBox;

UCLASS()
class CITIX_API UCitixNearbyRoomWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 FCitixNearbyRoom Room;
 int32 RoomIndex=0;
 virtual TSharedRef<SWidget> RebuildWidget() override;
 UFUNCTION() void JoinClicked();
 UPROPERTY() TObjectPtr<UButton> JoinButton;
};

UCLASS()
class CITIX_API UCitixChaseLobbyWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void NativeTick(const FGeometry& MyGeometry,float InDeltaTime) override;
private:
 UFUNCTION() void HostClicked();
 UFUNCTION() void HostAddressClicked();
 UFUNCTION() void FindClicked();
 UFUNCTION() void HostLANClicked();
 UFUNCTION() void FindLANClicked();
 UFUNCTION() void JoinClicked();
 UFUNCTION() void EnterLobbyClicked();
 UFUNCTION() void AdvancedClicked();
 UFUNCTION() void RetryClicked();
 UFUNCTION() void CancelClicked();
 UFUNCTION() void LeaveClicked();
 UFUNCTION() void CityMapClicked();
 UFUNCTION() void HillsideMapClicked();
 UPROPERTY() TObjectPtr<UButton> CityMapButton;
 UPROPERTY() TObjectPtr<UButton> HillsideMapButton;
 UPROPERTY() TObjectPtr<UTextBlock> MapLabel;
 void RefreshRooms();
 void TickUIProbe();
 UCitixSessionSubsystem* Manager() const;
 UPROPERTY() TObjectPtr<UTextBlock> StatusText;
 UPROPERTY() TObjectPtr<UTextBlock> DriverOne;
 UPROPERTY() TObjectPtr<UTextBlock> DriverTwo;
 UPROPERTY() TObjectPtr<UTextBlock> ReadyLabel;
 UPROPERTY() TObjectPtr<UTextBlock> RoomTitle;
 UPROPERTY() TObjectPtr<UTextBlock> RoomType;
 UPROPERTY() TObjectPtr<UTextBlock> ResultsLabel;
 UPROPERTY() TObjectPtr<UScrollBox> MainScroll;
 UPROPERTY() TObjectPtr<UButton> AdvancedToggle;
 UPROPERTY() TObjectPtr<UTextBlock> AdvancedLabel;
 UPROPERTY() TObjectPtr<UEditableTextBox> RoomEntry;
 UPROPERTY() TObjectPtr<UEditableTextBox> AddressEntry;
 UPROPERTY() TObjectPtr<UButton> HostButton;
 UPROPERTY() TObjectPtr<UButton> HostAddressButton;
 UPROPERTY() TObjectPtr<UButton> FindButton;
 UPROPERTY() TObjectPtr<UButton> HostLANButton;
 UPROPERTY() TObjectPtr<UButton> FindLANButton;
 UPROPERTY() TObjectPtr<UButton> JoinButton;
 UPROPERTY() TObjectPtr<UButton> EnterLobbyButton;
 UPROPERTY() TObjectPtr<UButton> RetryButton;
 UPROPERTY() TObjectPtr<UButton> CancelButton;
 UPROPERTY() TObjectPtr<UButton> LeaveButton;
 UPROPERTY() TObjectPtr<UVerticalBox> BrowserPanel;
 UPROPERTY() TObjectPtr<UVerticalBox> ConnectedPanel;
 UPROPERTY() TObjectPtr<UVerticalBox> RoomsBox;
 UPROPERTY() TObjectPtr<UVerticalBox> AdvancedPanel;
 bool bAdvanced=false;
 bool bTestClicked=false;
 bool bTestScreenshot=false;
 bool bTestFailureShot=false;
 bool bProbeListShot=false;
 bool bProbeConnectedShot=false;
 int32 SeenRevision=-1;
 float NextTestClick=5;
};
