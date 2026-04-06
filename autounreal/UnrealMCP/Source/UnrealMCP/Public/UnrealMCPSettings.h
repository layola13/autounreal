#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealMCPSettings.generated.h"

/**
 * Project settings for UnrealMCP plugin.
 * Visible in Editor: Project Settings -> Plugins -> UnrealMCP.
 */
UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="UnrealMCP"))
class UNREALMCP_API UUnrealMCPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUnrealMCPSettings();

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** Enable external graph integration fields below. */
	UPROPERTY(Config, EditAnywhere, Category="Neo4j")
	bool bEnableNeo4j = false;

	/** Neo4j bolt endpoint, for example: bolt://127.0.0.1:7687 */
	UPROPERTY(Config, EditAnywhere, Category="Neo4j", meta=(EditCondition="bEnableNeo4j", EditConditionHides))
	FString Neo4jUri = TEXT("bolt://127.0.0.1:7687");

	/** Neo4j username */
	UPROPERTY(Config, EditAnywhere, Category="Neo4j", meta=(EditCondition="bEnableNeo4j", EditConditionHides))
	FString Neo4jUser = TEXT("neo4j");

	/** Neo4j password */
	UPROPERTY(Config, EditAnywhere, Category="Neo4j", meta=(EditCondition="bEnableNeo4j", EditConditionHides, PasswordField=true))
	FString Neo4jPassword = TEXT("");

	/** Neo4j database name */
	UPROPERTY(Config, EditAnywhere, Category="Neo4j", meta=(EditCondition="bEnableNeo4j", EditConditionHides))
	FString Neo4jDatabase = TEXT("neo4j");
};

