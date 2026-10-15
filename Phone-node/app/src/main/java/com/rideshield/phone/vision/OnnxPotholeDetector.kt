package com.rideshield.phone.vision

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.RectF
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import kotlin.math.max
import kotlin.math.min

/**
 * On-device YOLO pothole detector powered by ONNX Runtime.
 * Loads the exported best.onnx model (from best.pt), runs preprocessing (letterbox to 640x640),
 * executes inference, and performs Non-Maximum Suppression (NMS).
 */
class OnnxPotholeDetector(
    private val context: Context? = null,
    private val modelAssetPath: String = "models/best.onnx",
    private val confidenceThreshold: Float = 0.35f,
    private val nmsIouThreshold: Float = 0.45f,
    private val clockProvider: () -> Long = { SystemClock.elapsedRealtime() }
) : PotholeDetector {

    private val tag = "OnnxPotholeDetector"
    private var isInitialized = false
    private var initError: String? = null

    // Reflection-based or direct ONNX runtime session handles to allow JVM testing without native crash
    private var ortEnvironment: Any? = null
    private var ortSession: Any? = null
    private var inputName: String = "images"

    companion object {
        const val INPUT_SIZE = 640
        const val NUM_BOXES = 8400
        const val NUM_CLASSES = 1 // Pothole
    }

    override fun initialize(): Boolean {
        if (isInitialized) return true

        try {
            // Attempt to load ONNX Runtime classes dynamically so unit tests without native lib don't crash
            val envClass = Class.forName("ai.onnxruntime.OrtEnvironment")
            val getEnvMethod = envClass.getMethod("getEnvironment")
            val env = getEnvMethod.invoke(null)
            ortEnvironment = env

            val optsClass = Class.forName("ai.onnxruntime.OrtSession\$SessionOptions")
            val opts = optsClass.getDeclaredConstructor().newInstance()

            val sessionClass = Class.forName("ai.onnxruntime.OrtSession")
            val modelBytes = loadModelBytes()
            if (modelBytes == null || modelBytes.isEmpty()) {
                initError = "Model file $modelAssetPath not found in assets or filesystem"
                Log.w(tag, initError!!)
                return false
            }

            val createSessionMethod = envClass.getMethod("createSession", ByteArray::class.java, optsClass)
            ortSession = createSessionMethod.invoke(env, modelBytes, opts)

            // Query input name
            val inputNamesMethod = sessionClass.getMethod("getInputNames")
            val inputNames = inputNamesMethod.invoke(ortSession) as? Set<*>
            inputName = (inputNames?.firstOrNull() as? String) ?: "images"

            isInitialized = true
            Log.i(tag, "ONNX Pothole Detector initialized successfully. Input: $inputName")
            return true
        } catch (e: ClassNotFoundException) {
            initError = "ONNX Runtime native libraries not available: ${e.message}"
            Log.w(tag, initError!!)
            return false
        } catch (e: Throwable) {
            initError = "Failed to initialize ONNX session: ${e.message}"
            Log.e(tag, initError!!, e)
            return false
        }
    }

    private fun loadModelBytes(): ByteArray? {
        if (context != null) {
            try {
                context.assets.open(modelAssetPath).use { it.readBytes() }
            } catch (e: Exception) {
                // Check app files dir
                val localFile = File(context.filesDir, modelAssetPath)
                if (localFile.exists()) return localFile.readBytes()
            }
        }

        // Check relative path for host development
        val directFile = File(modelAssetPath)
        if (directFile.exists()) return directFile.readBytes()

        val altPath = File("../Pothole-Computer-Vision-Project/best.onnx")
        if (altPath.exists()) return altPath.readBytes()

        return null
    }

    override fun detect(frameBitmap: Bitmap): PotholeDetectionResult {
        if (!isInitialized && !initialize()) {
            return PotholeDetectionResult(status = ModelStatus.ERROR)
        }

        val startMs = clockProvider()
        try {
            val (inputBuffer, scaleX, scaleY, padX, padY) = preprocessBitmap(frameBitmap)

            // Run ONNX inference via reflection
            val envClass = Class.forName("ai.onnxruntime.OrtEnvironment")
            val tensorClass = Class.forName("ai.onnxruntime.OnnxTensor")
            val createTensorMethod = tensorClass.getMethod(
                "createTensor",
                envClass,
                FloatBuffer::class.java,
                LongArray::class.java
            )

            val inputShape = longArrayOf(1, 3, INPUT_SIZE.toLong(), INPUT_SIZE.toLong())
            val tensor = createTensorMethod.invoke(null, ortEnvironment, inputBuffer, inputShape)

            val sessionClass = Class.forName("ai.onnxruntime.OrtSession")
            val runMethod = sessionClass.getMethod("run", Map::class.java)
            val inputs = mapOf(inputName to tensor)

            val resultObj = runMethod.invoke(ortSession, inputs)
            val resultClass = Class.forName("ai.onnxruntime.OrtSession\$Result")
            val getMethod = resultClass.getMethod("get", Int::class.javaPrimitiveType)
            val outputValue = getMethod.invoke(resultObj, 0)

            val onnxValueClass = Class.forName("ai.onnxruntime.OnnxValue")
            val getValueMethod = onnxValueClass.getMethod("getValue")
            val rawOutput = getValueMethod.invoke(outputValue)

            val detections = parseYoloOutput(rawOutput, frameBitmap.width, frameBitmap.height, scaleX, scaleY, padX, padY)
            val nmsDetections = applyNms(detections, nmsIouThreshold)

            val duration = clockProvider() - startMs
            val highestConf = if (nmsDetections.isNotEmpty()) (nmsDetections.maxOf { it.confidence } * 100).toInt() else 0
            val isHazard = highestConf >= 60

            return PotholeDetectionResult(
                potholes = nmsDetections,
                highestConfidence = highestConf,
                inferenceDurationMs = duration,
                isHazardPresent = isHazard,
                status = ModelStatus.LOADED
            )

        } catch (e: Exception) {
            Log.e(tag, "Inference error: ${e.message}", e)
            val duration = clockProvider() - startMs
            return PotholeDetectionResult(
                inferenceDurationMs = duration,
                status = ModelStatus.ERROR
            )
        }
    }

    private fun preprocessBitmap(src: Bitmap): PreprocessResult {
        val srcW = src.width
        val srcH = src.height

        val scale = min(INPUT_SIZE.toFloat() / srcW, INPUT_SIZE.toFloat() / srcH)
        val newW = (srcW * scale).toInt()
        val newH = (srcH * scale).toInt()

        val padX = (INPUT_SIZE - newW) / 2f
        val padY = (INPUT_SIZE - newH) / 2f

        val scaled = Bitmap.createScaledBitmap(src, newW, newH, true)

        val byteBuffer = ByteBuffer.allocateDirect(1 * 3 * INPUT_SIZE * INPUT_SIZE * 4)
            .order(ByteOrder.nativeOrder())
        val floatBuffer = byteBuffer.asFloatBuffer()

        val rPlane = FloatArray(INPUT_SIZE * INPUT_SIZE)
        val gPlane = FloatArray(INPUT_SIZE * INPUT_SIZE)
        val bPlane = FloatArray(INPUT_SIZE * INPUT_SIZE)

        // Fill background with 114/255 (standard YOLO letterbox color)
        val grayVal = 114f / 255f
        rPlane.fill(grayVal)
        gPlane.fill(grayVal)
        bPlane.fill(grayVal)

        val startX = padX.toInt()
        val startY = padY.toInt()

        val pixels = IntArray(newW * newH)
        scaled.getPixels(pixels, 0, newW, 0, 0, newW, newH)

        for (y in 0 until newH) {
            for (x in 0 until newW) {
                val p = pixels[y * newW + x]
                val targetIdx = (startY + y) * INPUT_SIZE + (startX + x)
                rPlane[targetIdx] = Color.red(p) / 255f
                gPlane[targetIdx] = Color.green(p) / 255f
                bPlane[targetIdx] = Color.blue(p) / 255f
            }
        }

        floatBuffer.put(rPlane)
        floatBuffer.put(gPlane)
        floatBuffer.put(bPlane)
        floatBuffer.rewind()

        if (scaled != src) scaled.recycle()

        return PreprocessResult(floatBuffer, scale, scale, padX, padY)
    }

    @Suppress("UNCHECKED_CAST")
    private fun parseYoloOutput(
        output: Any?,
        origW: Int,
        origH: Int,
        scaleX: Float,
        scaleY: Float,
        padX: Float,
        padY: Float
    ): List<BoundingBox> {
        val candidates = mutableListOf<BoundingBox>()
        if (output !is Array<*>) return candidates

        // Output shape for YOLOv12s detect: [1, 5, 8400]
        val batch = output[0] as? Array<FloatArray> ?: return candidates

        // If shape is [5][8400]:
        if (batch.size == 5 && batch[0].size >= NUM_BOXES) {
            val cxArray = batch[0]
            val cyArray = batch[1]
            val wArray = batch[2]
            val hArray = batch[3]
            val confArray = batch[4]

            for (i in 0 until min(cxArray.size, NUM_BOXES)) {
                val conf = confArray[i]
                if (conf < confidenceThreshold) continue

                val cx = cxArray[i]
                val cy = cyArray[i]
                val w = wArray[i]
                val h = hArray[i]

                // Map letterbox coords back to original frame bitmap coords
                val x1 = max(0f, ((cx - w / 2f) - padX) / scaleX)
                val y1 = max(0f, ((cy - h / 2f) - padY) / scaleY)
                val x2 = min(origW.toFloat(), ((cx + w / 2f) - padX) / scaleX)
                val y2 = min(origH.toFloat(), ((cy + h / 2f) - padY) / scaleY)

                if (x2 > x1 && y2 > y1) {
                    candidates.add(BoundingBox(RectF(x1, y1, x2, y2), conf, "Pothole"))
                }
            }
        }
        return candidates
    }

    private fun applyNms(boxes: List<BoundingBox>, iouThreshold: Float): List<BoundingBox> {
        val sorted = boxes.sortedByDescending { it.confidence }.toMutableList()
        val selected = mutableListOf<BoundingBox>()

        while (sorted.isNotEmpty()) {
            val current = sorted.removeAt(0)
            selected.add(current)

            val iterator = sorted.iterator()
            while (iterator.hasNext()) {
                val candidate = iterator.next()
                if (computeIoU(current.rect, candidate.rect) >= iouThreshold) {
                    iterator.remove()
                }
            }
        }
        return selected
    }

    private fun computeIoU(a: RectF, b: RectF): Float {
        val interLeft = max(a.left, b.left)
        val interTop = max(a.top, b.top)
        val interRight = min(a.right, b.right)
        val interBottom = min(a.bottom, b.bottom)

        val interArea = max(0f, interRight - interLeft) * max(0f, interBottom - interTop)
        val areaA = (a.right - a.left) * (a.bottom - a.top)
        val areaB = (b.right - b.left) * (b.bottom - b.top)
        val union = areaA + areaB - interArea
        return if (union > 0f) interArea / union else 0f
    }

    override fun getModelInfo(): ModelInfo {
        val status = if (isInitialized) ModelStatus.LOADED else if (initError != null) ModelStatus.ERROR else ModelStatus.UNAVAILABLE
        return ModelInfo(
            name = "YOLO Pothole Detector",
            version = "YOLOv12s-Pothole-1.0",
            status = status,
            description = "Custom trained YOLOv12 model running on-device ONNX runtime."
        )
    }

    override fun close() {
        isInitialized = false
    }

    private data class PreprocessResult(
        val buffer: FloatBuffer,
        val scaleX: Float,
        val scaleY: Float,
        val padX: Float,
        val padY: Float
    )
}
