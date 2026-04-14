// JVM stubs for kotlin.native annotations.
// These annotations only exist in Kotlin/Native metadata (.knm/.klib) and have no JVM .class files.
// kotlin-lsp cannot resolve them without JVM bytecode, so we provide minimal stubs.
// Source: kotlin-stdlib commonMain/kotlin/annotations/NativeAnnotations.kt (Apache 2.0)

package kotlin.native

@Target(AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.BINARY)
public annotation class CName(val externName: String = "", val shortName: String = "")

@Target(
  AnnotationTarget.CLASS,
  AnnotationTarget.ANNOTATION_CLASS,
  AnnotationTarget.PROPERTY,
  AnnotationTarget.FIELD,
  AnnotationTarget.LOCAL_VARIABLE,
  AnnotationTarget.VALUE_PARAMETER,
  AnnotationTarget.CONSTRUCTOR,
  AnnotationTarget.FUNCTION,
  AnnotationTarget.PROPERTY_GETTER,
  AnnotationTarget.PROPERTY_SETTER,
  AnnotationTarget.TYPEALIAS,
)
@Retention(AnnotationRetention.BINARY)
@Deprecated("Opting in for the freezing API is no longer supported.")
public annotation class FreezingIsDeprecated

@Target(
  AnnotationTarget.CLASS,
  AnnotationTarget.PROPERTY,
  AnnotationTarget.VALUE_PARAMETER,
  AnnotationTarget.FUNCTION,
)
@Retention(AnnotationRetention.BINARY)
@MustBeDocumented
public annotation class ObjCName(
  val name: String = "",
  val swiftName: String = "",
  val exact: Boolean = false,
)

@Target(AnnotationTarget.ANNOTATION_CLASS)
@Retention(AnnotationRetention.BINARY)
@MustBeDocumented
public annotation class HidesFromObjC

@HidesFromObjC
@Target(AnnotationTarget.PROPERTY, AnnotationTarget.FUNCTION, AnnotationTarget.CLASS)
@Retention(AnnotationRetention.BINARY)
@MustBeDocumented
public annotation class HiddenFromObjC

@Target(AnnotationTarget.ANNOTATION_CLASS)
@Retention(AnnotationRetention.BINARY)
@MustBeDocumented
public annotation class RefinesInSwift

@RefinesInSwift
@Target(AnnotationTarget.PROPERTY, AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.BINARY)
@MustBeDocumented
public annotation class ShouldRefineInSwift
