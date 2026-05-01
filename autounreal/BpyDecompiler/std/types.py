from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, TypeVar


@dataclass(frozen=True)
class Vector:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    @classmethod
    def parse(cls, value: str) -> "Vector":
        x, y, z = (float(part) for part in value.split(","))
        return cls(x, y, z)


TComponent = TypeVar("TComponent", bound="Component")


@dataclass
class Component:
    name: str
    type: str = "Component"
    parent: str | None = None
    properties: dict[str, Any] = field(default_factory=dict)

    def __getattr__(self, name: str) -> Any:
        try:
            return self.properties[name]
        except KeyError as exc:
            raise AttributeError(name) from exc

    def __getitem__(self, name: str) -> Any:
        return self.properties[name]


class ActorComponent(Component):
    pass


class SceneComponent(ActorComponent):
    pass


class PrimitiveComponent(SceneComponent):
    pass


class ShapeComponent(PrimitiveComponent):
    pass


class CapsuleComponent(ShapeComponent):
    pass


class MeshComponent(PrimitiveComponent):
    pass


class SkinnedMeshComponent(MeshComponent):
    pass


class SkeletalMeshComponent(SkinnedMeshComponent):
    pass


class CameraComponent(SceneComponent):
    pass


class SpringArmComponent(SceneComponent):
    pass


class ChildActorComponent(SceneComponent):
    pass


class GameplayCameraComponent(SceneComponent):
    pass


class GameplayCameraComponentBase(SceneComponent):
    pass


class MovementComponent(ActorComponent):
    pass


class MoverComponent(MovementComponent):
    pass


class CharacterMoverComponent(MoverComponent):
    pass


class NavMoverComponent(MovementComponent):
    pass


class MotionWarpingComponent(ActorComponent):
    pass


class AudioComponent(SceneComponent):
    pass


class UObject:
    pass


class BlueprintObject(UObject):
    def __init__(self) -> None:
        self.components: dict[str, Component] = {}

    def create_default_subobject(
        self,
        component_cls: type[TComponent],
        name: str,
        parent: Component | None = None,
        properties: dict[str, Any] | None = None,
    ) -> TComponent:
        component = component_cls(name=name, parent=parent.name if parent else None, properties=dict(properties or {}))
        self.components[name] = component
        return component


class Actor(BlueprintObject):
    pass


class Pawn(Actor):
    pass


class Character(Pawn):
    pass


class AnimInstance(BlueprintObject):
    pass


class AnimSingleNodeInstance(AnimInstance):
    pass


class AnimInstanceProxy(BlueprintObject):
    pass


class BlueprintGeneratedClass(BlueprintObject):
    pass


class AnimBlueprintGeneratedClass(BlueprintGeneratedClass):
    pass


class AnimNode(BlueprintObject):
    pass


class PoseLink(BlueprintObject):
    pass


class AnimSequenceBase(BlueprintObject):
    pass


class AnimSequence(AnimSequenceBase):
    pass


class AnimMontage(AnimSequenceBase):
    pass


class BlendSpace(AnimSequenceBase):
    pass


class SkeletalMesh(BlueprintObject):
    pass


class Skeleton(BlueprintObject):
    pass


class Controller(Actor):
    pass


class PlayerController(Controller):
    pass


class AIController(Controller):
    pass


class GameModeBase(Actor):
    pass


class GameMode(GameModeBase):
    pass


class GameStateBase(Actor):
    pass


class GameState(GameStateBase):
    pass


class PlayerState(Actor):
    pass


class PawnMovementComponent(MovementComponent):
    pass


class AnimationAsset(AnimSequenceBase):
    pass


class PoseSearchDatabase(BlueprintObject):
    pass


class PoseSearchHistory(BlueprintObject):
    pass


class PoseSearchResult(BlueprintObject):
    pass


class BlendStackAnimNode(AnimNode):
    pass


class MotionMatchingAnimNode(AnimNode):
    pass


class PoseSearchHistoryCollectorAnimNode(AnimNode):
    pass


class FootPlacementAnimNode(AnimNode):
    pass


class OrientationWarpingAnimNode(AnimNode):
    pass


class OffsetRootBoneAnimNode(AnimNode):
    pass


# Unreal-style aliases/classes for generated human code and editor type checking.
UObjectBase = UObject
UObjectBaseUtility = UObject
UObject = UObject
UBlueprintObject = BlueprintObject
UBlueprintGeneratedClass = BlueprintGeneratedClass
UAnimBlueprintGeneratedClass = AnimBlueprintGeneratedClass
UAnimInstance = AnimInstance
UAnimSingleNodeInstance = AnimSingleNodeInstance
FAnimInstanceProxy = AnimInstanceProxy
UAnimNode = AnimNode
UPoseLink = PoseLink
UAnimSequenceBase = AnimSequenceBase
UAnimSequence = AnimSequence
UAnimMontage = AnimMontage
UAnimationAsset = AnimationAsset
UBlendSpace = BlendSpace
UPoseSearchDatabase = PoseSearchDatabase
UPoseSearchHistory = PoseSearchHistory
UPoseSearchResult = PoseSearchResult
UBlendStackAnimNode = BlendStackAnimNode
UMotionMatchingAnimNode = MotionMatchingAnimNode
UPoseSearchHistoryCollectorAnimNode = PoseSearchHistoryCollectorAnimNode
UFootPlacementAnimNode = FootPlacementAnimNode
UOrientationWarpingAnimNode = OrientationWarpingAnimNode
UOffsetRootBoneAnimNode = OffsetRootBoneAnimNode
UController = Controller
UPlayerController = PlayerController
UAIController = AIController
UActor = Actor
AActor = Actor
APawn = Pawn
UPawn = Pawn
ACharacter = Character
UCharacter = Character
AController = Controller
APlayerController = PlayerController
AAIController = AIController
AGameModeBase = GameModeBase
AGameMode = GameMode
AGameStateBase = GameStateBase
AGameState = GameState
APlayerState = PlayerState
UActorComponent = ActorComponent
USceneComponent = SceneComponent
UPrimitiveComponent = PrimitiveComponent
UShapeComponent = ShapeComponent
UCapsuleComponent = CapsuleComponent
UMeshComponent = MeshComponent
USkinnedMeshComponent = SkinnedMeshComponent
USkeletalMeshComponent = SkeletalMeshComponent
UCameraComponent = CameraComponent
USpringArmComponent = SpringArmComponent
UChildActorComponent = ChildActorComponent
UGameplayCameraComponent = GameplayCameraComponent
UGameplayCameraComponentBase = GameplayCameraComponentBase
UMovementComponent = MovementComponent
UMoverComponent = MoverComponent
UCharacterMoverComponent = CharacterMoverComponent
UNavMoverComponent = NavMoverComponent
UMotionWarpingComponent = MotionWarpingComponent
UAudioComponent = AudioComponent
USkeletalMesh = SkeletalMesh
USkeleton = Skeleton
