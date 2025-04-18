// Copyright Epic Games, Inc. All Rights Reserved.

#include "GACharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY(LogTemplateAICharacter);

//////////////////////////////////////////////////////////////////////////
// AGACharacter

AGACharacter::AGACharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Configure character rotation
	// Should the character rotate towards the direction of movement?
	GetCharacterMovement()->bOrientRotationToMovement = true; 
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Initial movement frequency and amplitude
	MoveFrequency = 1.5f;
	MoveAmplitude = 1.0f;

}

void AGACharacter::BeginPlay()
{
	Super::BeginPlay();

}

void AGACharacter::Tick(float DeltaSeconds)
{

	Super::Tick(DeltaSeconds);
}